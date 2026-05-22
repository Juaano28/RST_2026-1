/*
 * button_led_module.c
 * Control del LED RGB #2 (ánodo común) como indicador de umbral de temperatura.
 *
 * ── Lógica general ───────────────────────────────────────────────────
 *
 *   Potenciómetro (GPIO1 / ADC_CHANNEL_1):
 *     raw ADC 0–4095 se mapea a un umbral de temperatura 0–100 °C.
 *     umbral = pot_raw * 100 / 4095
 *
 *   Comparación:
 *     Si temperatura_NTC > umbral → LED#2 enciende en ROJO (máx brillo)
 *     Si temperatura_NTC ≤ umbral → LED#2 apagado
 *
 *   Botón (GPIO9, activo en bajo):
 *     Cada pulsación cicla la unidad de temperatura activa:
 *       C → F → K → C → …
 *     La unidad queda guardada en uart_mod->temp_unit (via puntero).
 *
 * ── LED ánodo común — inversión de duty ──────────────────────────────
 *   El ánodo (+) está en VCC. El GPIO controla el cátodo (-).
 *   GPIO bajo → LED enciende → duty_ledc bajo = más brillo.
 *   duty_ledc = BTN_DUTY_MAX - duty_intuitivo
 */

#include "button_led_module.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BTN_LED";

#define BTN_DUTY_MAX  ((1u << BTN_LED_DUTY_RES) - 1)   /* 8191 con 13 bits */

/* ─────────────────────────────────────────────────────────────────────
 * invert_duty — cátodo-común a ánodo-común
 * ───────────────────────────────────────────────────────────────────── */
static inline uint32_t invert_duty(uint32_t duty)
{
    if (duty > BTN_DUTY_MAX) duty = BTN_DUTY_MAX;
    return BTN_DUTY_MAX - duty;
}

/* ─────────────────────────────────────────────────────────────────────
 * apply_rgb — escribe R, G, B con inversión de ánodo común
 * ───────────────────────────────────────────────────────────────────── */
static void apply_rgb(uint32_t r, uint32_t g, uint32_t b)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_R, invert_duty(r));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_R);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_G, invert_duty(g));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_G);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_B, invert_duty(b));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_B);
}

/* ─────────────────────────────────────────────────────────────────────
 * button_led_module_init
 * ───────────────────────────────────────────────────────────────────── */
void button_led_module_init(button_led_module_t *mod, temp_unit_t *temp_unit_ptr)
{
    mod->temp_unit_ptr   = temp_unit_ptr;
    mod->btn_last_level  = 1;
    mod->btn_last_tick_ms = 0;
    mod->umbral_celsius  = 30.0f;
    mod->led_on          = false;

    /* ── Botón: entrada con pull-up interno ──────────────────────────── */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    /* ── Timer LEDC para LED#2 (TIMER_1) ────────────────────────────── */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BTN_LED_DUTY_RES,
        .timer_num       = BTN_LED_LEDC_TIMER,
        .freq_hz         = BTN_LED_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* ── Tres canales PWM — duty inicial = BTN_DUTY_MAX → apagado ────────── */
    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = BTN_LED_LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .hpoint     = 0,
        .duty       = BTN_DUTY_MAX,   /* ánodo común: BTN_DUTY_MAX = apagado */
    };

    ch.channel  = BTN_LED_CHANNEL_R;
    ch.gpio_num = BTN_LED_RED_GPIO;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    ch.channel  = BTN_LED_CHANNEL_G;
    ch.gpio_num = BTN_LED_GREEN_GPIO;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    ch.channel  = BTN_LED_CHANNEL_B;
    ch.gpio_num = BTN_LED_BLUE_GPIO;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    /* Confirmar apagado explícito */
    apply_rgb(0, 0, 0);

    ESP_LOGI(TAG, "LED#2 anodo comun: R=GPIO%d G=GPIO%d B=GPIO%d  Boton=GPIO%d",
             (int)BTN_LED_RED_GPIO, (int)BTN_LED_GREEN_GPIO,
             (int)BTN_LED_BLUE_GPIO, (int)BTN_GPIO);
    ESP_LOGI(TAG, "Boton cicla unidad: C -> F -> K -> C");
    ESP_LOGI(TAG, "POT = umbral de temperatura 0-100 C para LED2");
}

/* ─────────────────────────────────────────────────────────────────────
 * button_led_unit_name
 * ───────────────────────────────────────────────────────────────────── */
const char *button_led_unit_name(temp_unit_t unit)
{
    switch (unit) {
        case TEMP_UNIT_F: return "Fahrenheit (F)";
        case TEMP_UNIT_K: return "Kelvin (K)";
        default:          return "Celsius (C)";
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * button_led_get_umbral
 * ───────────────────────────────────────────────────────────────────── */
float button_led_get_umbral(const button_led_module_t *mod)
{
    return mod->umbral_celsius;
}

/* ─────────────────────────────────────────────────────────────────────
 * button_led_module_process — llamar cada ~20 ms
 *
 * @param ntc_celsius  Temperatura actual del NTC en °C.
 *                     La tarea led1_temp ya la leyó; se la pasamos
 *                     para evitar una lectura extra del ADC.
 * ───────────────────────────────────────────────────────────────────── */
void button_led_module_process(button_led_module_t *mod,
                                adc_module_t *adc,
                                float ntc_celsius)
{
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    /* ── 1. Leer botón con antirebote ───────────────────────────────── */
    int level = gpio_get_level(BTN_GPIO);

    if (mod->btn_last_level == 1 && level == 0) {
        /* Flanco descendente detectado */
        if ((now_ms - mod->btn_last_tick_ms) >= BTN_DEBOUNCE_MS) {
            mod->btn_last_tick_ms = now_ms;

            /* Avanzar unidad: C → F → K → C */
            if (mod->temp_unit_ptr != NULL) {
                switch (*mod->temp_unit_ptr) {
                    case TEMP_UNIT_C: *mod->temp_unit_ptr = TEMP_UNIT_F; break;
                    case TEMP_UNIT_F: *mod->temp_unit_ptr = TEMP_UNIT_K; break;
                    default:          *mod->temp_unit_ptr = TEMP_UNIT_C; break;
                }
                ESP_LOGI(TAG, "Unidad cambiada a: %s",
                         button_led_unit_name(*mod->temp_unit_ptr));
            }
        }
    }
    mod->btn_last_level = level;

    /* ── 2. Leer POT → calcular umbral (0 – 100 °C) ────────────────── */
    int pot_raw, pot_mv;
    adc_module_read_pot(adc, &pot_raw, &pot_mv);
    mod->umbral_celsius = (float)pot_raw * (UMBRAL_TEMP_MAX - UMBRAL_TEMP_MIN)
                          / 4095.0f + UMBRAL_TEMP_MIN;

    /* ── 3. Comparar temperatura vs umbral → controlar LED#2 ─────────── */
    bool encender = (ntc_celsius > mod->umbral_celsius);

    if (encender != mod->led_on) {
        mod->led_on = encender;
        if (encender) {
            /* Temperatura supera umbral → LED2 rojo máximo brillo */
            apply_rgb(BTN_DUTY_MAX, 0, 0);
            ESP_LOGI(TAG, "LED2 ROJO encendido: T=%.2fC > umbral=%.2fC",
                     ntc_celsius, mod->umbral_celsius);
        } else {
            /* Temperatura bajo umbral → LED2 apagado */
            apply_rgb(0, 0, 0);
            ESP_LOGI(TAG, "LED2 apagado: T=%.2fC <= umbral=%.2fC",
                     ntc_celsius, mod->umbral_celsius);
        }
    }
}