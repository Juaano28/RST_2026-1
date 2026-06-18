#ifndef BUTTON_LED_MODULE_H
#define BUTTON_LED_MODULE_H

/*
 * button_led_module.h
 * Módulo de control del LED RGB #2 mediante botón y potenciómetro.
 *
 * ── Lógica de los 4 estados ──────────────────────────────────────────
 *
 *   Estado 0 — AJUSTE ROJO:
 *     El potenciómetro controla SOLO el canal rojo en tiempo real.
 *     Al pasar al siguiente estado, el valor actual del rojo se GUARDA.
 *
 *   Estado 1 — AJUSTE AZUL:
 *     El rojo queda fijo en el valor guardado del estado anterior.
 *     El potenciómetro controla SOLO el canal azul en tiempo real.
 *     Al pasar al siguiente estado, el valor actual del azul se GUARDA.
 *
 *   Estado 2 — AJUSTE VERDE:
 *     Rojo y azul quedan fijos. El potenciómetro controla SOLO el verde.
 *     Al pasar al siguiente estado, el valor actual del verde se GUARDA.
 *
 *   Estado 3 — COMBINADO (R + G + B):
 *     Se aplican simultáneamente los tres valores guardados.
 *     El potenciómetro ya no cambia nada en este estado.
 *     Al pulsar de nuevo el botón vuelve al estado 0 y el rojo
 *     vuelve a ser controlable (los valores guardados se resetean).
 *
 * ── LED #2: ánodo común ──────────────────────────────────────────────
 *   En ánodo común el LED se enciende con duty BAJO (0 = máximo brillo)
 *   y se apaga con duty ALTO (DUTY_MAX = apagado).
 *   Por eso el duty se INVIERTE antes de escribir al LEDC:
 *     duty_ledc = DUTY_MAX - duty_calculado
 *   Así cuando el potenciómetro está al máximo (duty=8191) el LEDC
 *   recibe 0 → brillo máximo. Al mínimo (duty=0) recibe 8191 → apagado.
 *
 * ── LED #1: cátodo común ─────────────────────────────────────────────
 *   El LED #1 (temperatura) es cátodo común — duty alto = más brillo.
 *   Este módulo NO afecta al LED #1.
 *
 * ── Hardware ─────────────────────────────────────────────────────────
 *   LED RGB #2 (ánodo común):
 *     R  → GPIO10
 *     G  → GPIO11
 *     B  → GPIO15   (GPIO12=USB_D- y GPIO13=USB_D+, NO USAR)
 *
 *   Botón → GPIO9  (activo en bajo, pull-up interno)
 *   Potenciómetro → GPIO1 (ADC_CHANNEL_1, compartido con adc_module)
 *
 * ── Canales LEDC ─────────────────────────────────────────────────────
 *   LEDC_TIMER_1 + CHANNEL_3/4/5  (separados del LED #1: TIMER_0 CH0/1/2)
 */

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "adc_module.h"
#include <stdint.h>

/* ── GPIOs del segundo LED RGB ──────────────────────────────────────── */
#define BTN_LED_RED_GPIO    GPIO_NUM_10
#define BTN_LED_GREEN_GPIO  GPIO_NUM_11
#define BTN_LED_BLUE_GPIO   GPIO_NUM_15  /* GPIO12/13 = USB → usar GPIO15 */

/* ── GPIO del botón ─────────────────────────────────────────────────── */
#define BTN_GPIO            GPIO_NUM_9

/* ── Canales LEDC ────────────────────────────────────────────────────── */
#define BTN_LED_LEDC_TIMER  LEDC_TIMER_1
#define BTN_LED_CHANNEL_R   LEDC_CHANNEL_3
#define BTN_LED_CHANNEL_G   LEDC_CHANNEL_4
#define BTN_LED_CHANNEL_B   LEDC_CHANNEL_5
#define BTN_LED_DUTY_RES    LEDC_TIMER_13_BIT   /* 0 – 8191 */
#define BTN_LED_FREQ_HZ     1000

/* ── Antirebote ──────────────────────────────────────────────────────── */
#define BTN_DEBOUNCE_MS     50

/* ── Número de estados ───────────────────────────────────────────────── */
#define BTN_LED_NUM_STATES  4

/* ── Umbral mínimo de duty (evita encendido residual al mínimo del POT) */
#define BTN_DUTY_THRESHOLD  80u

/* ── Estados de color ───────────────────────────────────────────────── */
typedef enum {
    BTN_LED_STATE_RED   = 0,   /* ajustar rojo con POT          */
    BTN_LED_STATE_BLUE  = 1,   /* ajustar azul con POT          */
    BTN_LED_STATE_GREEN = 2,   /* ajustar verde con POT         */
    BTN_LED_STATE_RGB   = 3,   /* mostrar R+G+B guardados       */
} btn_led_state_t;

/* ── Contexto del módulo ─────────────────────────────────────────────── */
typedef struct {
    btn_led_state_t  state;

    /* Valores guardados al avanzar de estado */
    uint32_t         saved_red;    /* duty rojo guardado al salir del estado 0 */
    uint32_t         saved_blue;   /* duty azul guardado al salir del estado 1 */
    uint32_t         hsaved_green;  /* duty verde guardado al salir del estado 2 */

    uint32_t         duty_resolution;
    int              btn_last_level;
    uint32_t         btn_last_tick_ms;
} button_led_module_t;

/* ── API pública ─────────────────────────────────────────────────────── */

/** Inicializa LED #2 (ánodo común) y el botón. Arranca apagado en estado ROJO. */
void button_led_module_init(button_led_module_t *mod);

/**
 * Procesar cada ~20 ms desde el loop principal.
 * Lee el botón con antirebote y el potenciómetro,
 * aplica la lógica de estados y actualiza el LED #2.
 */
void button_led_module_process(button_led_module_t *mod, adc_module_t *adc);

/** Nombre del estado actual como string (para logs y UART). */
const char *button_led_state_name(btn_led_state_t state);

#endif /* BUTTON_LED_MODULE_H */