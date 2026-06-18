/*
 * library_led_c.c
 * Librería para control de LED RGB por PWM (LEDC) en ESP32-C6.
 * Cada pulsador incrementa en 10 % el duty cycle de su canal.
 */

#include "library_led_c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ─────────────────────────────────────────────────────────────────────
 * config_led_rgb
 * Inicializa el timer LEDC y los tres canales (R, G, B).
 * Debe llamarse una sola vez al inicio.
 * ───────────────────────────────────────────────────────────────────── */
void config_led_rgb(led_rgb_t *led_rgb)
{
    /* --- Timer compartido para los tres canales --- */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = led_rgb->speed_mode,
        .duty_resolution = led_rgb->duty_resolution,
        .timer_num       = led_rgb->timer,
        .freq_hz         = led_rgb->frequency,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* --- Canal ROJO --- */
    ledc_channel_config_t ch_red = {
        .speed_mode = led_rgb->speed_mode,
        .channel    = led_rgb->led_red.channel,
        .timer_sel  = led_rgb->timer,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = led_rgb->led_red.gpio_num,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_red));

    /* --- Canal VERDE --- */
    ledc_channel_config_t ch_green = {
        .speed_mode = led_rgb->speed_mode,
        .channel    = led_rgb->led_green.channel,
        .timer_sel  = led_rgb->timer,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = led_rgb->led_green.gpio_num,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_green));

    /* --- Canal AZUL --- */
    ledc_channel_config_t ch_blue = {
        .speed_mode = led_rgb->speed_mode,
        .channel    = led_rgb->led_blue.channel,
        .timer_sel  = led_rgb->timer,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = led_rgb->led_blue.gpio_num,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_blue));
}

/* ─────────────────────────────────────────────────────────────────────
 * set_led_rgb_given_struct
 * Aplica los duty cycles que están guardados dentro del struct.
 * ───────────────────────────────────────────────────────────────────── */
void set_led_rgb_given_struct(led_rgb_t *led_rgb)
{
    ESP_ERROR_CHECK(ledc_set_duty(led_rgb->speed_mode,
                                  led_rgb->led_red.channel,
                                  led_rgb->led_red.duty));
    ESP_ERROR_CHECK(ledc_update_duty(led_rgb->speed_mode,
                                     led_rgb->led_red.channel));

    ESP_ERROR_CHECK(ledc_set_duty(led_rgb->speed_mode,
                                  led_rgb->led_green.channel,
                                  led_rgb->led_green.duty));
    ESP_ERROR_CHECK(ledc_update_duty(led_rgb->speed_mode,
                                     led_rgb->led_green.channel));

    ESP_ERROR_CHECK(ledc_set_duty(led_rgb->speed_mode,
                                  led_rgb->led_blue.channel,
                                  led_rgb->led_blue.duty));
    ESP_ERROR_CHECK(ledc_update_duty(led_rgb->speed_mode,
                                     led_rgb->led_blue.channel));
}

/* ─────────────────────────────────────────────────────────────────────
 * set_led_rgb_percentage_given_values
 * Recibe porcentajes enteros (0-100) y los convierte al rango del timer.
 * Ejemplo: 50 % con resolución 13-bit → duty = (8191 * 50) / 100 = 4095
 * ───────────────────────────────────────────────────────────────────── */
void set_led_rgb_percentage_given_values(led_rgb_t *led_rgb,
                                          int percentage_red,
                                          int percentage_green,
                                          int percentage_blue)
{
    /* Calcular el duty máximo según la resolución configurada */
    uint32_t max_duty = (1u << led_rgb->duty_resolution) - 1;

    led_rgb->led_red.duty   = (max_duty * (uint32_t)percentage_red)   / 100;
    led_rgb->led_green.duty = (max_duty * (uint32_t)percentage_green) / 100;
    led_rgb->led_blue.duty  = (max_duty * (uint32_t)percentage_blue)  / 100;

    set_led_rgb_given_struct(led_rgb);
}

/* ─────────────────────────────────────────────────────────────────────
 * set_led_rgb_given_values
 * Aplica valores absolutos de duty cycle directamente.
 * ───────────────────────────────────────────────────────────────────── */
void set_led_rgb_given_values(led_rgb_t *led_rgb,
                               uint32_t duty_red,
                               uint32_t duty_green,
                               uint32_t duty_blue)
{
    led_rgb->led_red.duty   = duty_red;
    led_rgb->led_green.duty = duty_green;
    led_rgb->led_blue.duty  = duty_blue;

    set_led_rgb_given_struct(led_rgb);
}

/* ─────────────────────────────────────────────────────────────────────
 * config_buttons
 * Configura los tres pines de botón como entradas digitales
 * con resistencia de pull-down interna habilitada.
 * ───────────────────────────────────────────────────────────────────── */
void config_buttons(void)
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BTN_RED_GPIO)  |
                        (1ULL << BTN_GREEN_GPIO) |
                        (1ULL << BTN_BLUE_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* sin pulsar = 0 lógico */
        .intr_type    = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));
}

/* ─────────────────────────────────────────────────────────────────────
 * handle_buttons
 * Función de polling: lee los tres botones y, si alguno está presionado,
 * incrementa en DUTY_STEP (10 %) el canal correspondiente.
 * Incluye un pequeño debounce por software (espera a que se suelte).
 * ───────────────────────────────────────────────────────────────────── */
void handle_buttons(led_rgb_t *led_rgb)
{
    /* --- Botón ROJO --- */
    if (gpio_get_level(BTN_RED_GPIO) == 1) {
        /* Debounce: esperar a que se suelte el botón */
        while (gpio_get_level(BTN_RED_GPIO) == 1) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        /* Incrementar duty, saturando en DUTY_MAX */
        led_rgb->led_red.duty += DUTY_STEP;
        if (led_rgb->led_red.duty > DUTY_MAX) {
            led_rgb->led_red.duty = 0;   /* vuelta a 0 tras llegar al 100 % */
        }
        ESP_ERROR_CHECK(ledc_set_duty(led_rgb->speed_mode,
                                       led_rgb->led_red.channel,
                                       led_rgb->led_red.duty));
        ESP_ERROR_CHECK(ledc_update_duty(led_rgb->speed_mode,
                                          led_rgb->led_red.channel));
        printf("[BTN] RED   → duty = %lu (%.0f%%)\n",
               led_rgb->led_red.duty,
               (float)led_rgb->led_red.duty * 100.0f / DUTY_MAX);
    }

    /* --- Botón VERDE --- */
    if (gpio_get_level(BTN_GREEN_GPIO) == 1) {
        while (gpio_get_level(BTN_GREEN_GPIO) == 1) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        led_rgb->led_green.duty += DUTY_STEP;
        if (led_rgb->led_green.duty > DUTY_MAX) {
            led_rgb->led_green.duty = 0;
        }
        ESP_ERROR_CHECK(ledc_set_duty(led_rgb->speed_mode,
                                       led_rgb->led_green.channel,
                                       led_rgb->led_green.duty));
        ESP_ERROR_CHECK(ledc_update_duty(led_rgb->speed_mode,
                                          led_rgb->led_green.channel));
        printf("[BTN] GREEN → duty = %lu (%.0f%%)\n",
               led_rgb->led_green.duty,
               (float)led_rgb->led_green.duty * 100.0f / DUTY_MAX);
    }

    /* --- Botón AZUL --- */
    if (gpio_get_level(BTN_BLUE_GPIO) == 1) {
        while (gpio_get_level(BTN_BLUE_GPIO) == 1) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        led_rgb->led_blue.duty += DUTY_STEP;
        if (led_rgb->led_blue.duty > DUTY_MAX) {
            led_rgb->led_blue.duty = 0;
        }
        ESP_ERROR_CHECK(ledc_set_duty(led_rgb->speed_mode,
                                       led_rgb->led_blue.channel,
                                       led_rgb->led_blue.duty));
        ESP_ERROR_CHECK(ledc_update_duty(led_rgb->speed_mode,
                                          led_rgb->led_blue.channel));
        printf("[BTN] BLUE  → duty = %lu (%.0f%%)\n",
               led_rgb->led_blue.duty,
               (float)led_rgb->led_blue.duty * 100.0f / DUTY_MAX);
    }
}