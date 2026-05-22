#ifndef ADC_MODULE_H
#define ADC_MODULE_H

/*
 * adc_module.h
 * Módulo de lectura ADC con calibración para ESP32-C6.
 *
 * El ESP32-C6 soporta únicamente el esquema de calibración
 * ADC_CALI_SCHEME_VER_CURVE_FITTING, que usa una curva polinómica
 * almacenada en eFuse para corregir la no-linealidad del ADC.
 *
 * Se configuran dos canales:
 *   - Canal NTC  (termistor)   → ADC_CHANNEL_0 = GPIO0
 *   - Canal POT  (potenciómetro) → ADC_CHANNEL_1 = GPIO1
 */

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <stdbool.h>
#include <stdint.h>

/* ── GPIOs / Canales ADC ────────────────────────────────────────────── */
/* En ESP32-C6: ADC_CHANNEL_N corresponde a GPIO_N para ADC_UNIT_1     */
#define ADC_NTC_CHANNEL     ADC_CHANNEL_0   /* GPIO0 → termistor NTC    */
#define ADC_POT_CHANNEL     ADC_CHANNEL_1   /* GPIO1 → potenciómetro    */
#define ADC_UNIT            ADC_UNIT_1
#define ADC_ATTEN           ADC_ATTEN_DB_12 /* rango 0–3100 mV aprox.   */
#define ADC_MULTISAMPLING   16              /* promedio de 16 lecturas   */

/* ── Handle del módulo ADC ─────────────────────────────────────────── */
typedef struct {
    adc_oneshot_unit_handle_t handle;
    adc_cali_handle_t         cali_handle_ntc;
    adc_cali_handle_t         cali_handle_pot;
    bool                      cali_enabled;
} adc_module_t;

/* ── API pública ─────────────────────────────────────────────────────── */

/**
 * Inicializa el ADC en modo oneshot con calibración Curve Fitting.
 * Configura los dos canales (NTC y POT) con atenuación de 12 dB.
 * Si la calibración no está disponible en eFuse, opera en modo raw.
 */
void adc_module_init(adc_module_t *adc);

/**
 * Lee el canal del NTC.
 * out_raw_mv: voltaje calibrado en mV (o raw si sin calibración)
 * out_raw:    valor crudo 0–4095
 */
void adc_module_read_ntc(adc_module_t *adc, int *out_raw, int *out_mv);

/**
 * Lee el canal del potenciómetro.
 */
void adc_module_read_pot(adc_module_t *adc, int *out_raw, int *out_mv);

/**
 * Libera los recursos del módulo ADC.
 */
void adc_module_deinit(adc_module_t *adc);

#endif /* ADC_MODULE_H */
