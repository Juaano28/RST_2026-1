/*
 * ledc_basic_example_main.c
 * Punto de entrada del proyecto LED RGB con control por pulsadores.
 * Cada pulsador incrementa en 10 % el duty cycle de su canal PWM.
 *
 * Pines LED : R=GPIO2  G=GPIO4  B=GPIO5
 * Pines BTN : R=GPIO6  G=GPIO7  B=GPIO10
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "library_led_c.h"

void app_main(void)
{
    printf("=== LED RGB con control por pulsadores ===\n");

    /* ── 1. Definir la configuración del LED RGB ─────────────────────── */
    led_rgb_t led_rgb = {
        .led_red = {
            .gpio_num = GPIO_NUM_2,
            .channel  = LEDC_CHANNEL_0,
            .duty     = 0             /* arranca apagado */
        },
        .led_green = {
            .gpio_num = GPIO_NUM_4,
            .channel  = LEDC_CHANNEL_1,
            .duty     = 0
        },
        .led_blue = {
            .gpio_num = GPIO_NUM_5,
            .channel  = LEDC_CHANNEL_2,
            .duty     = 0
        },
        .timer          = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT, /* rango: 0 – 8191 */
        .frequency      = 1000,               /* 1 kHz — buena frecuencia para LEDs */
        .speed_mode     = LEDC_LOW_SPEED_MODE
    };

    /* ── 2. Inicializar LEDC y GPIOs de botones ──────────────────────── */
    config_led_rgb(&led_rgb);
    config_buttons();

    printf("Configuración lista. Presiona los botones para cambiar intensidad.\n");
    printf("  BTN en GPIO%d → LED Rojo\n",   BTN_RED_GPIO);
    printf("  BTN en GPIO%d → LED Verde\n",  BTN_GREEN_GPIO);
    printf("  BTN en GPIO%d → LED Azul\n",   BTN_BLUE_GPIO);
    printf("Cada pulsación suma 10%% de intensidad (vuelve a 0 al superar 100%%).\n\n");

    /* ── 3. Loop principal: polling de botones cada 20 ms ────────────── */
    while (1) {
        handle_buttons(&led_rgb);
        vTaskDelay(pdMS_TO_TICKS(20));  /* cede CPU al scheduler cada 20 ms */
    }
}