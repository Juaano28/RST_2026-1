#include "control_system.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "adc_module.h"
#include "ntc_module.h"
#include "registers.h"
#include "rgb_led.h"
#include "wifi_app.h"
#include "esp_check.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define FAN_LEDC_TIMER      LEDC_TIMER_0
#define FAN_LEDC_CHANNEL    LEDC_CHANNEL_0
#define FAN_LEDC_FREQ_HZ    25000
#define FAN_DUTY_RES        LEDC_TIMER_10_BIT
#define FAN_DUTY_MAX        ((1 << 10) - 1)

#define RGB_LEDC_TIMER      LEDC_TIMER_1
#define RGB_R_CHANNEL       LEDC_CHANNEL_1
#define RGB_G_CHANNEL       LEDC_CHANNEL_2
#define RGB_B_CHANNEL       LEDC_CHANNEL_3
#define RGB_LEDC_FREQ_HZ    1000
#define RGB_DUTY_RES        LEDC_TIMER_8_BIT
#define RGB_DUTY_MAX        255

#define SERVO_LEDC_TIMER    LEDC_TIMER_2
#define SERVO_LEDC_CHANNEL  LEDC_CHANNEL_4
#define SERVO_LEDC_FREQ_HZ  50
#define SERVO_DUTY_RES      LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX      ((1 << 14) - 1)
#define SERVO_MIN_US        1000
#define SERVO_MAX_US        2000
#define SERVO_PERIOD_US     20000

#define CONTROL_TASK_STACK  6144
#define CONTROL_TASK_PRIO   5
#define CONTROL_TASK_MS     1000

static const char *TAG = "CONTROL";
static const char *NVS_NS = "ctrl_cfg";
static const char *NVS_KEY = "state_v1";

static SemaphoreHandle_t s_state_mutex;
static TaskHandle_t s_control_task_handle;

static control_state_t s_state = {
    .temperature_c = 25.0f,
    .adc_raw = 0,
    .adc_mv = 0,
    .temperature_valid = false,
    .thermal_mode = CONTROL_MODE_AUTO,
    .desired_temp_c = CONTROL_DEFAULT_TEMP_DESIRED_C,
    .max_temp_c = CONTROL_DEFAULT_TEMP_MAX_C,
    .manual_fan_percent = 0,
    .fan_percent = 0,
    .alarm_active = false,
    .curtain_mode = CONTROL_MODE_MANUAL,
    .curtain_percent = 0,
    .rgb_r = 0,
    .rgb_g = 128,
    .rgb_b = 255,
    .rgb_brightness = 50,
    .time_synchronized = false,
};

typedef struct {
    control_mode_t thermal_mode;
    float desired_temp_c;
    float max_temp_c;
    uint8_t manual_fan_percent;
    control_mode_t curtain_mode;
    uint8_t curtain_percent;
    uint8_t rgb_r;
    uint8_t rgb_g;
    uint8_t rgb_b;
    uint8_t rgb_brightness;
} persisted_control_config_t;

static uint8_t clamp_u8(uint8_t value, uint8_t max)
{
    return value > max ? max : value;
}

static float clamp_float(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void lock_state(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
}

static void unlock_state(void)
{
    xSemaphoreGive(s_state_mutex);
}

static esp_err_t fan_pwm_set(uint8_t percent)
{
    percent = clamp_u8(percent, 100);
    uint32_t duty = (FAN_DUTY_MAX * percent) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CHANNEL, duty), TAG, "fan set duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CHANNEL);
}

static esp_err_t rgb_pwm_set(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    brightness = clamp_u8(brightness, 100);
    uint32_t rd = ((uint32_t)r * brightness) / 100;
    uint32_t gd = ((uint32_t)g * brightness) / 100;
    uint32_t bd = ((uint32_t)b * brightness) / 100;

    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, RGB_R_CHANNEL, rd), TAG, "rgb r duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, RGB_R_CHANNEL), TAG, "rgb r update");
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, RGB_G_CHANNEL, gd), TAG, "rgb g duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, RGB_G_CHANNEL), TAG, "rgb g update");
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, RGB_B_CHANNEL, bd), TAG, "rgb b duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, RGB_B_CHANNEL);
}

static esp_err_t servo_pwm_set(uint8_t percent)
{
    percent = clamp_u8(percent, 100);
    uint32_t pulse_us = SERVO_MIN_US + ((SERVO_MAX_US - SERVO_MIN_US) * percent) / 100;
    uint32_t duty = (SERVO_DUTY_MAX * pulse_us) / SERVO_PERIOD_US;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_LEDC_CHANNEL, duty), TAG, "servo duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_LEDC_CHANNEL);
}

static void configure_ledc(void)
{
    ledc_timer_config_t fan_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = FAN_LEDC_TIMER,
        .duty_resolution = FAN_DUTY_RES,
        .freq_hz = FAN_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&fan_timer));

    ledc_timer_config_t rgb_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = RGB_LEDC_TIMER,
        .duty_resolution = RGB_DUTY_RES,
        .freq_hz = RGB_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&rgb_timer));

    ledc_timer_config_t servo_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = SERVO_LEDC_TIMER,
        .duty_resolution = SERVO_DUTY_RES,
        .freq_hz = SERVO_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&servo_timer));

    ledc_channel_config_t fan_ch = {
        .gpio_num = CONTROL_FAN_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = FAN_LEDC_CHANNEL,
        .timer_sel = FAN_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&fan_ch));

    const int rgb_gpios[3] = {RGB_LED_RED_GPIO, RGB_LED_GREEN_GPIO, RGB_LED_BLUE_GPIO};
    const ledc_channel_t rgb_channels[3] = {RGB_R_CHANNEL, RGB_G_CHANNEL, RGB_B_CHANNEL};
    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t rgb_ch = {
            .gpio_num = rgb_gpios[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = rgb_channels[i],
            .timer_sel = RGB_LEDC_TIMER,
            .intr_type = LEDC_INTR_DISABLE,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&rgb_ch));
    }

    ledc_channel_config_t servo_ch = {
        .gpio_num = CONTROL_SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = SERVO_LEDC_CHANNEL,
        .timer_sel = SERVO_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&servo_ch));
}

static uint8_t compute_fan_percent(float temp_c, float desired_c, float max_c)
{
    if (!isfinite(temp_c) || temp_c <= -200.0f) return 0;
    if (max_c <= desired_c + 0.5f) max_c = desired_c + 0.5f;
    if (temp_c <= desired_c) return 0;
    if (temp_c >= max_c) return 100;
    return (uint8_t)roundf(((temp_c - desired_c) * 100.0f) / (max_c - desired_c));
}

static bool schedule_matches_now(const timer_register_t *reg, const struct tm *timeinfo)
{
    if (!reg || !reg->active) return false;
    int monday_based_day = (timeinfo->tm_wday + 6) % 7;
    return reg->days[monday_based_day] &&
           reg->hour == timeinfo->tm_hour &&
           reg->minutes == timeinfo->tm_min &&
           timeinfo->tm_sec < 3;
}

static void apply_schedule_if_needed(void)
{
    control_state_t snapshot;
    control_system_get_state(&snapshot);
    if (snapshot.curtain_mode != CONTROL_MODE_AUTO || !get_state_time_was_synchronized()) {
        return;
    }

    time_t now;
    struct tm timeinfo;
    time(&now);
    if (localtime_r(&now, &timeinfo) == NULL) return;

    timer_register_t regs[MAX_REGISTERS];
    registers_copy_all(regs, MAX_REGISTERS);
    for (int i = 0; i < MAX_REGISTERS; i++) {
        if (schedule_matches_now(&regs[i], &timeinfo)) {
            control_system_set_curtain(CONTROL_MODE_AUTO, regs[i].curtain_percent);
            ESP_LOGI(TAG, "Agenda activa: reg=%d cortina=%u%%", i + 1, regs[i].curtain_percent);
            break;
        }
    }
}

static void control_task(void *arg)
{
    adc_module_t adc;
    memset(&adc, 0, sizeof(adc));
    adc_module_init(&adc);

    bool alarm_gpio_level = false;

    for (;;) {
        int raw = 0, mv = 0;
        adc_module_read_ntc(&adc, &raw, &mv);
        float temp = ntc_voltage_to_celsius(mv);
        bool temp_valid = isfinite(temp) && temp > -100.0f;

        lock_state();
        s_state.adc_raw = raw;
        s_state.adc_mv = mv;
        s_state.temperature_c = temp_valid ? temp : s_state.temperature_c;
        s_state.temperature_valid = temp_valid;
        s_state.time_synchronized = get_state_time_was_synchronized();

        uint8_t target_fan = s_state.manual_fan_percent;
        if (s_state.thermal_mode == CONTROL_MODE_AUTO) {
            target_fan = compute_fan_percent(s_state.temperature_c, s_state.desired_temp_c, s_state.max_temp_c);
        }
        s_state.fan_percent = target_fan;
        s_state.alarm_active = temp_valid && (s_state.temperature_c > s_state.max_temp_c);

        control_state_t local = s_state;
        unlock_state();

        fan_pwm_set(local.fan_percent);
        servo_pwm_set(local.curtain_percent);
        rgb_pwm_set(local.rgb_r, local.rgb_g, local.rgb_b, local.rgb_brightness);

        if (local.alarm_active) {
            alarm_gpio_level = !alarm_gpio_level;
            gpio_set_level(CONTROL_ALARM_LED_GPIO, alarm_gpio_level);
        } else {
            alarm_gpio_level = false;
            gpio_set_level(CONTROL_ALARM_LED_GPIO, 0);
        }

        apply_schedule_if_needed();
        ESP_LOGI(TAG, "T=%.2fC raw=%d mv=%d fan=%u%% curtain=%u%% alarm=%d",
                 local.temperature_c, local.adc_raw, local.adc_mv, local.fan_percent,
                 local.curtain_percent, local.alarm_active);
        vTaskDelay(pdMS_TO_TICKS(CONTROL_TASK_MS));
    }
}

void control_system_init(void)
{
    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
        configASSERT(s_state_mutex);
    }

    gpio_config_t alarm_cfg = {
        .pin_bit_mask = 1ULL << CONTROL_ALARM_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&alarm_cfg));
    gpio_set_level(CONTROL_ALARM_LED_GPIO, 0);

    configure_ledc();
    registers_init();
    control_system_load_config();

    fan_pwm_set(s_state.fan_percent);
    servo_pwm_set(s_state.curtain_percent);
    rgb_pwm_set(s_state.rgb_r, s_state.rgb_g, s_state.rgb_b, s_state.rgb_brightness);
}

void control_system_start(void)
{
    if (s_control_task_handle == NULL) {
        xTaskCreate(control_task, "control_task", CONTROL_TASK_STACK, NULL, CONTROL_TASK_PRIO, &s_control_task_handle);
    }
}

esp_err_t control_system_get_state(control_state_t *out_state)
{
    if (!out_state || !s_state_mutex) return ESP_ERR_INVALID_ARG;
    lock_state();
    *out_state = s_state;
    unlock_state();
    return ESP_OK;
}

esp_err_t control_system_set_thermal(control_mode_t mode, float desired_temp_c, float max_temp_c, uint8_t manual_fan_percent)
{
    if (!s_state_mutex) return ESP_ERR_INVALID_STATE;
    desired_temp_c = clamp_float(desired_temp_c, -20.0f, 100.0f);
    max_temp_c = clamp_float(max_temp_c, desired_temp_c + 0.5f, 120.0f);
    manual_fan_percent = clamp_u8(manual_fan_percent, 100);

    lock_state();
    s_state.thermal_mode = mode;
    s_state.desired_temp_c = desired_temp_c;
    s_state.max_temp_c = max_temp_c;
    s_state.manual_fan_percent = manual_fan_percent;
    unlock_state();
    return control_system_save_config();
}

esp_err_t control_system_set_curtain(control_mode_t mode, uint8_t curtain_percent)
{
    if (!s_state_mutex) return ESP_ERR_INVALID_STATE;
    curtain_percent = clamp_u8(curtain_percent, 100);
    lock_state();
    s_state.curtain_mode = mode;
    s_state.curtain_percent = curtain_percent;
    unlock_state();
    servo_pwm_set(curtain_percent);
    return control_system_save_config();
}

esp_err_t control_system_set_rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    if (!s_state_mutex) return ESP_ERR_INVALID_STATE;
    brightness = clamp_u8(brightness, 100);
    lock_state();
    s_state.rgb_r = r;
    s_state.rgb_g = g;
    s_state.rgb_b = b;
    s_state.rgb_brightness = brightness;
    unlock_state();
    rgb_pwm_set(r, g, b, brightness);
    return control_system_save_config();
}

esp_err_t control_system_save_config(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    control_state_t snap;
    control_system_get_state(&snap);
    persisted_control_config_t cfg = {
        .thermal_mode = snap.thermal_mode,
        .desired_temp_c = snap.desired_temp_c,
        .max_temp_c = snap.max_temp_c,
        .manual_fan_percent = snap.manual_fan_percent,
        .curtain_mode = snap.curtain_mode,
        .curtain_percent = snap.curtain_percent,
        .rgb_r = snap.rgb_r,
        .rgb_g = snap.rgb_g,
        .rgb_b = snap.rgb_b,
        .rgb_brightness = snap.rgb_brightness,
    };

    err = nvs_set_blob(h, NVS_KEY, &cfg, sizeof(cfg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t control_system_load_config(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Sin configuración previa de control; usando defaults");
        return err;
    }

    persisted_control_config_t cfg;
    size_t len = sizeof(cfg);
    err = nvs_get_blob(h, NVS_KEY, &cfg, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(cfg)) {
        ESP_LOGW(TAG, "Config de control no encontrada o incompatible; usando defaults");
        return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    }

    lock_state();
    s_state.thermal_mode = cfg.thermal_mode == CONTROL_MODE_AUTO ? CONTROL_MODE_AUTO : CONTROL_MODE_MANUAL;
    s_state.desired_temp_c = clamp_float(cfg.desired_temp_c, -20.0f, 100.0f);
    s_state.max_temp_c = clamp_float(cfg.max_temp_c, s_state.desired_temp_c + 0.5f, 120.0f);
    s_state.manual_fan_percent = clamp_u8(cfg.manual_fan_percent, 100);
    s_state.curtain_mode = cfg.curtain_mode == CONTROL_MODE_AUTO ? CONTROL_MODE_AUTO : CONTROL_MODE_MANUAL;
    s_state.curtain_percent = clamp_u8(cfg.curtain_percent, 100);
    s_state.rgb_r = cfg.rgb_r;
    s_state.rgb_g = cfg.rgb_g;
    s_state.rgb_b = cfg.rgb_b;
    s_state.rgb_brightness = clamp_u8(cfg.rgb_brightness, 100);
    unlock_state();

    ESP_LOGI(TAG, "Configuración de control cargada desde NVS");
    return ESP_OK;
}
