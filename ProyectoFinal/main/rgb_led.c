#include "rgb_led.h"

#include <stdbool.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

#define RGB_TIMER        LEDC_TIMER_1
#define RGB_FREQ_HZ      1000
#define RGB_DUTY_RES     LEDC_TIMER_8_BIT
#define RGB_MODE         LEDC_LOW_SPEED_MODE

static const char *TAG = "RGB_LED";
static bool s_rgb_init = false;

void rgb_led_init(void)
{
    if (s_rgb_init) return;

    ledc_timer_config_t timer = {
        .speed_mode = RGB_MODE,
        .timer_num = RGB_TIMER,
        .duty_resolution = RGB_DUTY_RES,
        .freq_hz = RGB_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const int gpios[3] = {RGB_LED_RED_GPIO, RGB_LED_GREEN_GPIO, RGB_LED_BLUE_GPIO};
    const ledc_channel_t channels[3] = {LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3};

    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t ch = {
            .gpio_num = gpios[i],
            .speed_mode = RGB_MODE,
            .channel = channels[i],
            .timer_sel = RGB_TIMER,
            .intr_type = LEDC_INTR_DISABLE,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ch));
    }

    s_rgb_init = true;
    ESP_LOGI(TAG, "RGB LED inicializado en LEDC_LOW_SPEED_MODE/TIMER_1");
}

void rgb_led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!s_rgb_init) rgb_led_init();

    ledc_set_duty(RGB_MODE, LEDC_CHANNEL_1, red);
    ledc_update_duty(RGB_MODE, LEDC_CHANNEL_1);
    ledc_set_duty(RGB_MODE, LEDC_CHANNEL_2, green);
    ledc_update_duty(RGB_MODE, LEDC_CHANNEL_2);
    ledc_set_duty(RGB_MODE, LEDC_CHANNEL_3, blue);
    ledc_update_duty(RGB_MODE, LEDC_CHANNEL_3);
}

void rgb_led_wifi_app_started(void)
{
    rgb_led_set_color(255, 102, 255);
}

void rgb_led_http_server_started(void)
{
    rgb_led_set_color(204, 255, 51);
}

void rgb_led_wifi_connected(void)
{
    rgb_led_set_color(0, 255, 153);
}
