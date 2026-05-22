#ifndef BUTTON_LED_MODULE_H
#define BUTTON_LED_MODULE_H

/*
 * button_led_module.h
 * Módulo rediseñado: botón + potenciómetro + LED#2.
 *
 * ── Nueva función del botón ───────────────────────────────────────────
 *   Cada pulsación avanza la unidad de temperatura activa:
 *     C  →  F  →  K  →  C  →  …
 *   La unidad se sincroniza con uart_module_t->temp_unit.
 *
 * ── Nueva función del potenciómetro ──────────────────────────────────
 *   El POT fija un umbral de temperatura entre 0 y 100 °C.
 *   Si la temperatura NTC supera ese umbral, el LED#2 se enciende
 *   en ROJO (intensidad fija al máximo).
 *   Si la temperatura es menor al umbral, el LED#2 se apaga.
 *
 * ── LED #2: ánodo común ──────────────────────────────────────────────
 *   duty_ledc = DUTY_MAX - duty_intuitivo
 *   duty=0   → DUTY_MAX en LEDC → GPIO alto → LED apagado
 *   duty=MAX → 0 en LEDC       → GPIO bajo  → LED máximo brillo
 *
 * ── Hardware ─────────────────────────────────────────────────────────
 *   LED RGB #2 (ánodo común):
 *     R  → GPIO10
 *     G  → GPIO11
 *     B  → GPIO15
 *   Botón → GPIO9  (activo en bajo, pull-up interno)
 *   Potenciómetro → GPIO1 (ADC_CHANNEL_1)
 *
 * ── Canales LEDC ─────────────────────────────────────────────────────
 *   LEDC_TIMER_1 + CHANNEL_3/4/5
 */

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "adc_module.h"
#include "uart_module.h"   /* temp_unit_t */
#include <stdint.h>
#include <stdbool.h>

/* ── GPIOs del segundo LED RGB ──────────────────────────────────────── */
#define BTN_LED_RED_GPIO    GPIO_NUM_10
#define BTN_LED_GREEN_GPIO  GPIO_NUM_11
#define BTN_LED_BLUE_GPIO   GPIO_NUM_15

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

/* ── Rango del umbral de temperatura (°C) ───────────────────────────── */
#define UMBRAL_TEMP_MIN     0.0f
#define UMBRAL_TEMP_MAX     100.0f

/* ── Contexto del módulo ─────────────────────────────────────────────── */
typedef struct {
    /* Unidad de temperatura activa (apuntador a uart_mod->temp_unit) */
    temp_unit_t     *temp_unit_ptr;

    /* Antirebote del botón */
    int              btn_last_level;
    uint32_t         btn_last_tick_ms;

    /* Umbral calculado a partir del POT (°C) */
    float            umbral_celsius;

    /* Último estado del LED (para no escribir si no cambió) */
    bool             led_on;
} button_led_module_t;

/* ── API pública ─────────────────────────────────────────────────────── */

/**
 * Inicializa LED#2 (ánodo común) y el botón.
 * @param temp_unit_ptr  Puntero a uart_mod->temp_unit para sincronizar.
 */
void button_led_module_init(button_led_module_t *mod, temp_unit_t *temp_unit_ptr);

/**
 * Procesar cada ~20 ms desde el loop principal.
 * - Lee el botón: avanza unidad C→F→K→C.
 * - Lee el POT: calcula umbral 0-100°C.
 * - Lee NTC: si temp > umbral → LED2 rojo encendido, si no → apagado.
 *
 * @param ntc_celsius   Temperatura NTC en °C (ya leída por task_led1).
 */
void button_led_module_process(button_led_module_t *mod,
                                adc_module_t *adc,
                                float ntc_celsius);

/** Retorna el umbral actual en °C. */
float button_led_get_umbral(const button_led_module_t *mod);

/** Nombre de la unidad activa como string. */
const char *button_led_unit_name(temp_unit_t unit);

#endif /* BUTTON_LED_MODULE_H */
