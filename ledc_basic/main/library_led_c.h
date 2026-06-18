#ifndef LIBRARY_LED_C_H
#define LIBRARY_LED_C_H

#include "driver/ledc.h"   /* requiere REQUIRES esp_driver_ledc en CMakeLists */
#include "driver/gpio.h"   /* requiere REQUIRES esp_driver_gpio  en CMakeLists */
#include <stdio.h>
#include <stdint.h>

/* ── Pines de los botones ───────────────────────────────────────────── */
#define BTN_RED_GPIO    GPIO_NUM_6
#define BTN_GREEN_GPIO  GPIO_NUM_7
#define BTN_BLUE_GPIO   GPIO_NUM_10

/* ── Paso de incremento por pulsación (10 % del rango 13-bit = 819) ── */
#define DUTY_STEP       819   /* 10 % de 8191 */
#define DUTY_MAX        8191  /* 2^13 - 1 */

/* ── Struct de un canal LED individual ─────────────────────────────── */
typedef struct {
    uint32_t        duty;
    gpio_num_t      gpio_num;
    ledc_channel_t  channel;
} led_t;

/* ── Struct del LED RGB completo ────────────────────────────────────── */
typedef struct {
    led_t            led_red;
    led_t            led_green;
    led_t            led_blue;
    ledc_timer_t     timer;
    ledc_timer_bit_t duty_resolution;
    uint32_t         frequency;
    ledc_mode_t      speed_mode;
} led_rgb_t;

/* ── API pública ─────────────────────────────────────────────────────── */

/** Configura el timer LEDC y los tres canales PWM según el struct */
void config_led_rgb(led_rgb_t *led_rgb);

/** Aplica los duty cycles almacenados en el struct */
void set_led_rgb_given_struct(led_rgb_t *led_rgb);

/** Aplica duty cycles expresados como porcentaje (0-100) */
void set_led_rgb_percentage_given_values(led_rgb_t *led_rgb,
                                         int percentage_red,
                                         int percentage_green,
                                         int percentage_blue);

/** Aplica duty cycles directamente como valores absolutos */
void set_led_rgb_given_values(led_rgb_t *led_rgb,
                               uint32_t duty_red,
                               uint32_t duty_green,
                               uint32_t duty_blue);

/** Configura los tres GPIOs de botón como entradas con pull-down */
void config_buttons(void);

/** Lee el estado de los botones y aumenta 10 % el canal correspondiente */
void handle_buttons(led_rgb_t *led_rgb);

#endif /* LIBRARY_LED_C_H */