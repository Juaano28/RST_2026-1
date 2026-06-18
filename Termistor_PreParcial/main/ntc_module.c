/*
 * ntc_module.c
 * Conversión voltaje → resistencia → temperatura para termistor NTC.
 * Modelo: divisor de voltaje + ecuación Beta de Steinhart-Hart.
 */

#include "ntc_module.h"
#include "esp_log.h"
#include <math.h>   /* logf(), necesita -lm en el linker (ESP-IDF lo incluye) */

static const char *TAG = "NTC";

/* ─────────────────────────────────────────────────────────────────────
 * ntc_voltage_to_resistance
 *
 * El circuito es un divisor de voltaje:
 *   Vcc ── R_serie ── Vout ── NTC ── GND
 *
 * Despejando NTC del divisor:
 *   Vout = Vcc * R_ntc / (R_serie + R_ntc)
 *   Vout * R_serie + Vout * R_ntc = Vcc * R_ntc
 *   Vout * R_serie = R_ntc * (Vcc - Vout)
 *   R_ntc = (R_serie * Vout) / (Vcc - Vout)
 * ───────────────────────────────────────────────────────────────────── */
float ntc_voltage_to_resistance(int voltage_mv)
{
    float vout = (float)voltage_mv;

    /* Protección contra división por cero */
    if (vout <= 0.0f)             return 1e9f;   /* cortocircuito: ∞ fría */
    if (vout >= NTC_VCC - 1.0f)   return 0.0f;   /* NTC abierto: ∞ caliente */

    return (NTC_R_SERIE * vout) / (NTC_VCC - vout);
}

/* ─────────────────────────────────────────────────────────────────────
 * ntc_resistance_to_celsius
 *
 * Ecuación Beta de Steinhart-Hart:
 *   1/T = 1/T0 + (1/Beta) * ln(R/R0)
 *   T = 1 / (1/T0 + (1/Beta) * ln(R/R0))   (en Kelvin)
 *   T_celsius = T - 273.15
 * ───────────────────────────────────────────────────────────────────── */
float ntc_resistance_to_celsius(float resistance_ohm)
{
    if (resistance_ohm <= 0.0f) {
        ESP_LOGW(TAG, "Resistencia NTC inválida: %.1f Ω", resistance_ohm);
        return -273.15f;
    }

    float ln_r = logf(resistance_ohm / NTC_R0);
    float inv_T = (1.0f / NTC_T0_KELVIN) + (1.0f / NTC_BETA) * ln_r;
    float T_kelvin = 1.0f / inv_T;
    return T_kelvin - 273.15f;
}

/* ─────────────────────────────────────────────────────────────────────
 * ntc_voltage_to_celsius
 * Combina las dos funciones anteriores en un solo paso conveniente.
 * ───────────────────────────────────────────────────────────────────── */
float ntc_voltage_to_celsius(int voltage_mv)
{
    float r = ntc_voltage_to_resistance(voltage_mv);
    float t = ntc_resistance_to_celsius(r);

    ESP_LOGD(TAG, "Vout=%d mV  R_ntc=%.1f Ω  T=%.2f °C", voltage_mv, r, t);

    return t;
}
