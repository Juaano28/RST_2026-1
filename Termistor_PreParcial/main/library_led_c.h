#ifndef LIBRARY_LED_C_H
#define LIBRARY_LED_C_H

/*
 * library_led_c.h
 * Módulo de control del LED RGB por PWM (LEDC).
 * Tres canales independientes: R, G, B.
 * Cada canal tiene límites min/max configurables por UART.
 */

#include "driver/ledc.h"
#include "driver/gpio.h"
#include <stdint.h>

/* ── GPIOs del LED RGB ──────────────────────────────────────────────── */
#define LED_RED_GPIO    GPIO_NUM_2
#define LED_GREEN_GPIO  GPIO_NUM_4
#define LED_BLUE_GPIO   GPIO_NUM_5

/* ── Resolución del timer LEDC: 13 bits → rango 0–8191 ─────────────── */
#define DUTY_MAX        8191
#define DUTY_STEP       819     /* 10% de 8191 */

/* ── Struct de un canal LED individual ─────────────────────────────── */
typedef struct {
    uint32_t       duty;        /* valor actual (0 – 8191)              */
    uint32_t       duty_min;    /* límite mínimo configurable por UART  */
    uint32_t       duty_max;    /* límite máximo configurable por UART  */
    gpio_num_t     gpio_num;
    ledc_channel_t channel;
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

/** Inicializa timer LEDC y los tres canales PWM. duty arranca en 0. */
void config_led_rgb(led_rgb_t *led_rgb);

/** Aplica los duty del struct al hardware LEDC. */
void set_led_rgb_given_struct(led_rgb_t *led_rgb);

/**
 * Ajusta el color del LED RGB según temperatura (°C).
 * Mapea la temperatura al rango min–max de cada canal configurado por UART.
 *   < temp_min → Azul puro  (frío)
 *   = temp_mid → Verde puro (templado)
 *   > temp_max → Rojo puro  (caliente)
 */
void set_led_rgb_by_temperature(led_rgb_t *led_rgb, float temperature,
                                 float temp_min, float temp_max);

/** Convierte porcentaje (0-100) a valor de duty absoluto según resolución. */
uint32_t percent_to_duty(led_rgb_t *led_rgb, int percent);

#endif /* LIBRARY_LED_C_H */
