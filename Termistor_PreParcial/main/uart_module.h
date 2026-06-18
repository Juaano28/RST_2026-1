#ifndef UART_MODULE_H
#define UART_MODULE_H

/*
 * uart_module.h
 * Módulo UART con comandos de control y modo calibración ADC.
 *
 * Comandos disponibles:
 *
 *   SET_R <min> <max>    Límites LED rojo   (porcentaje 0-100)
 *   SET_G <min> <max>    Límites LED verde  (porcentaje 0-100)
 *   SET_B <min> <max>    Límites LED azul   (porcentaje 0-100)
 *   READ_LED_VALUES      Duty cycles actuales R, G, B
 *   READ_TEMP            Temperatura NTC + voltaje + resistencia
 *   READ_POT             Voltaje raw y calibrado del potenciómetro
 *   CAL_MODE             Entra en modo calibración continua (muestra
 *                        datos cada segundo hasta escribir EXIT)
 *   HELP                 Lista de comandos
 */

#include "library_led_c.h"
#include "adc_module.h"
#include "ntc_module.h"
#include <stdbool.h>

/* ── Configuración UART ─────────────────────────────────────────────── */
#define UART_PORT           UART_NUM_0
#define UART_BAUD_RATE      115200
#define UART_BUF_SIZE       512
#define UART_CMD_MAX_LEN    64

/* ── Estado del módulo ──────────────────────────────────────────────── */
typedef struct {
    led_rgb_t    *led_rgb;
    adc_module_t *adc;
    char          cmd_buf[UART_CMD_MAX_LEN];
    int           cmd_len;
    bool          cal_mode_active;   /* true = modo calibración activo */
    uint32_t      cal_tick_counter;  /* para imprimir cada ~1 segundo  */
} uart_module_t;

/* ── API pública ─────────────────────────────────────────────────────── */
void uart_module_init(uart_module_t *uart_mod, led_rgb_t *led, adc_module_t *adc);
void uart_module_process(uart_module_t *uart_mod);

#endif /* UART_MODULE_H */