#ifndef UART_MODULE_H
#define UART_MODULE_H

/*
 * uart_module.h
 * Módulo UART con comandos de control y modo calibración ADC.
 *
 * Comandos disponibles:
 *
 *   SET_R <min> <max>              Límites duty LED rojo   (porcentaje 0-100)
 *   SET_G <min> <max>              Límites duty LED verde  (porcentaje 0-100)
 *   SET_B <min> <max>              Límites duty LED azul   (porcentaje 0-100)
 *   SET_TEMP_RANGE <Rmin> <Rmax> <Gmin> <Gmax> <Bmin> <Bmax>
 *                                  Límites de temperatura (°C) para cada canal
 *                                  del LED#1. Ej: SET_TEMP_RANGE 15 25 25 35 35 45
 *   TEMP <seg> <unidad>            Impresión periódica de temperatura.
 *                                  <seg> = 0 desactiva. Unidades: C, F, K
 *                                  Ej: TEMP 5 F
 *   READ_TEMP                      Temperatura NTC instantánea (en unidad actual)
 *   READ_LED_VALUES                Duty cycles actuales R, G, B
 *   READ_POT                       Voltaje y porcentaje del potenciómetro
 *   READ_UMBRAL                    Umbral actual del LED#2 en °C
 *   CAL_MODE                       Modo calibración continua (EXIT para salir)
 *   HELP                           Lista de comandos
 */

#include "library_led_c.h"
#include "adc_module.h"
#include "ntc_module.h"
#include <stdbool.h>
#include <stdint.h>

/* ── Configuración UART ─────────────────────────────────────────────── */
#define UART_PORT           UART_NUM_0
#define UART_BAUD_RATE      115200
#define UART_BUF_SIZE       512
#define UART_CMD_MAX_LEN    80

/* ── Unidades de temperatura ────────────────────────────────────────── */
typedef enum {
    TEMP_UNIT_C = 0,   /* Celsius   */
    TEMP_UNIT_F,       /* Fahrenheit */
    TEMP_UNIT_K,       /* Kelvin    */
} temp_unit_t;

/* ── Estado del módulo ──────────────────────────────────────────────── */
typedef struct {
    led_rgb_t    *led_rgb;
    adc_module_t *adc;
    char          cmd_buf[UART_CMD_MAX_LEN];
    int           cmd_len;

    /* Modo calibración */
    bool          cal_mode_active;
    uint32_t      cal_tick_counter;

    /* Impresión periódica de temperatura (TEMP <seg> <unidad>) */
    bool          temp_print_active;
    uint32_t      temp_print_interval_ticks; /* ticks de 20ms */
    uint32_t      temp_print_counter;
    temp_unit_t   temp_unit;                 /* C / F / K      */

    /* Límites de temperatura para el gradiente del LED#1 (en °C) */
    float         temp_range_r_min;   /* °C inicio del rojo   */
    float         temp_range_r_max;   /* °C fin del rojo      */
    float         temp_range_g_min;   /* °C inicio del verde  */
    float         temp_range_g_max;   /* °C fin del verde     */
    float         temp_range_b_min;   /* °C inicio del azul   */
    float         temp_range_b_max;   /* °C fin del azul      */

} uart_module_t;

/* ── API pública ─────────────────────────────────────────────────────── */
void uart_module_init(uart_module_t *uart_mod, led_rgb_t *led, adc_module_t *adc);
void uart_module_process(uart_module_t *uart_mod);

/* Getters usados por otras tareas */
temp_unit_t  uart_get_temp_unit(const uart_module_t *uart_mod);
void         uart_get_temp_range(const uart_module_t *uart_mod,
                                  float *r_min, float *r_max,
                                  float *g_min, float *g_max,
                                  float *b_min, float *b_max);

#endif /* UART_MODULE_H */
