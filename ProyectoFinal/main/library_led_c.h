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
#define LED_RED_GPIO    GPIO_NUM_4
#define LED_GREEN_GPIO  GPIO_NUM_5
#define LED_BLUE_GPIO   GPIO_NUM_6

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
 * Ajusta el LED RGB#1 según temperatura con rangos INDEPENDIENTES por canal.
 *
 * Cada color tiene su propio rango [min, max] en °C:
 *   - Si temp está DENTRO del rango [min, max] del canal → ese color enciende
 *     al 100% de su duty_max configurado.
 *   - Si temp está FUERA del rango → ese canal se apaga (duty=0).
 *
 * Los rangos pueden solaparse libremente:
 *   - Rango R [25,35]  Rango G [25,35]  Rango B [25,35] y temp=30°C
 *     → los TRES colores encienden al mismo tiempo (blanco).
 *   - Rango R [30,40]  Rango G [20,30]  Rango B [20,30] y temp=25°C
 *     → solo verde y azul encienden (cian).
 *
 * @param temperature  Temperatura actual en °C
 * @param r_min/r_max  Rango °C en que el canal ROJO  enciende
 * @param g_min/g_max  Rango °C en que el canal VERDE enciende
 * @param b_min/b_max  Rango °C en que el canal AZUL  enciende
 */
void set_led_rgb_by_temperature(led_rgb_t *led_rgb, float temperature,
                                 float r_min, float r_max,
                                 float g_min, float g_max,
                                 float b_min, float b_max);

/** Convierte porcentaje (0-100) a valor de duty absoluto según resolución. */
uint32_t percent_to_duty(led_rgb_t *led_rgb, int percent);
// Agrega estas declaraciones antes del #endif en library_led_c.h
void rgb_led_wifi_app_started(void);
void rgb_led_http_server_started(void);
void rgb_led_wifi_connected(void);

// Declara la instancia global para que wifi_app pueda verla
extern led_rgb_t my_rgb_led;

#endif /* LIBRARY_LED_C_H */