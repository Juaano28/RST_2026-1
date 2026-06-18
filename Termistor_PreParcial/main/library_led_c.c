/*
 * library_led_c.c
 * Implementación del control PWM para LED RGB.
 * Usa el periférico LEDC del ESP32-C6.
 */

#include "library_led_c.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "LED_RGB";

/* Declaración forward: función interna, no visible fuera de este archivo */
static void apply_with_clamp(led_rgb_t *led_rgb,
                              uint32_t r, uint32_t g, uint32_t b);

/* ─────────────────────────────────────────────────────────────────────
 * percent_to_duty
 * ───────────────────────────────────────────────────────────────────── */
uint32_t percent_to_duty(led_rgb_t *led_rgb, int percent)
{
    uint32_t max = (1u << led_rgb->duty_resolution) - 1;
    if (percent <= 0)   return 0;
    if (percent >= 100) return max;
    return (max * (uint32_t)percent) / 100;
}

/* ─────────────────────────────────────────────────────────────────────
 * config_led_rgb
 * ───────────────────────────────────────────────────────────────────── */
void config_led_rgb(led_rgb_t *led_rgb)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = led_rgb->speed_mode,
        .duty_resolution = led_rgb->duty_resolution,
        .timer_num       = led_rgb->timer,
        .freq_hz         = led_rgb->frequency,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch = {
        .speed_mode = led_rgb->speed_mode,
        .timer_sel  = led_rgb->timer,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = 0,
        .hpoint     = 0,
    };

    ch.channel  = led_rgb->led_red.channel;
    ch.gpio_num = led_rgb->led_red.gpio_num;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    ch.channel  = led_rgb->led_green.channel;
    ch.gpio_num = led_rgb->led_green.gpio_num;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    ch.channel  = led_rgb->led_blue.channel;
    ch.gpio_num = led_rgb->led_blue.gpio_num;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    ESP_LOGI(TAG, "LED RGB: R=GPIO%d G=GPIO%d B=GPIO%d @ %luHz",
             (int)led_rgb->led_red.gpio_num,
             (int)led_rgb->led_green.gpio_num,
             (int)led_rgb->led_blue.gpio_num,
             led_rgb->frequency);
}

/* ─────────────────────────────────────────────────────────────────────
 * set_led_rgb_given_struct
 * ───────────────────────────────────────────────────────────────────── */
void set_led_rgb_given_struct(led_rgb_t *led_rgb)
{
    led_t *leds[3] = {
        &led_rgb->led_red,
        &led_rgb->led_green,
        &led_rgb->led_blue
    };
    for (int i = 0; i < 3; i++) {
        if (leds[i]->duty < leds[i]->duty_min) leds[i]->duty = leds[i]->duty_min;
        if (leds[i]->duty > leds[i]->duty_max) leds[i]->duty = leds[i]->duty_max;
        ESP_ERROR_CHECK(ledc_set_duty(led_rgb->speed_mode,
                                       leds[i]->channel, leds[i]->duty));
        ESP_ERROR_CHECK(ledc_update_duty(led_rgb->speed_mode, leds[i]->channel));
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * set_led_rgb_by_temperature
 * ───────────────────────────────────────────────────────────────────── */
void set_led_rgb_by_temperature(led_rgb_t *led_rgb, float temperature,
                                 float temp_min, float temp_max)
{
    uint32_t max_duty = (1u << led_rgb->duty_resolution) - 1;
    float ratio;

    if (temperature <= temp_min)       ratio = 0.0f;
    else if (temperature >= temp_max)  ratio = 1.0f;
    else ratio = (temperature - temp_min) / (temp_max - temp_min);

    uint32_t r, g, b;

    if (ratio < 0.5f) {
        float t = ratio * 2.0f;
        b = (uint32_t)(max_duty * (1.0f - t));
        g = (uint32_t)(max_duty * t);
        r = 0;
    } else {
        float t = (ratio - 0.5f) * 2.0f;
        g = (uint32_t)(max_duty * (1.0f - t));
        r = (uint32_t)(max_duty * t);
        b = 0;
    }

    apply_with_clamp(led_rgb, r, g, b);
}

/* ─────────────────────────────────────────────────────────────────────
 * apply_with_clamp  — función interna
 * ───────────────────────────────────────────────────────────────────── */
static void apply_with_clamp(led_rgb_t *led_rgb,
                               uint32_t r, uint32_t g, uint32_t b)
{
    led_rgb->led_red.duty   = r;
    led_rgb->led_green.duty = g;
    led_rgb->led_blue.duty  = b;
    set_led_rgb_given_struct(led_rgb);
}
