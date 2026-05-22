#ifndef NTC_MODULE_H
#define NTC_MODULE_H

/*
 * ntc_module.h
 * Módulo de conversión voltaje → temperatura para termistor NTC.
 *
 * Un NTC (Negative Temperature Coefficient) es una resistencia cuyo
 * valor DISMINUYE cuando la temperatura AUMENTA. Se usa en un divisor
 * de voltaje con una resistencia fija (R_serie) para medir temperatura.
 *
 * Circuito:
 *   3.3V ── R_serie (10kΩ) ── Vout ── NTC ── GND
 *
 * La fórmula de Steinhart-Hart simplificada (modelo Beta) convierte
 * la resistencia del NTC a temperatura en Kelvin:
 *
 *   1/T = 1/T0 + (1/Beta) * ln(R/R0)
 *
 * donde:
 *   T   = temperatura medida (Kelvin)
 *   T0  = temperatura de referencia del NTC (25°C = 298.15 K)
 *   R0  = resistencia del NTC a T0 (típicamente 10kΩ)
 *   R   = resistencia medida del NTC
 *   Beta = coeficiente Beta del NTC (típicamente 3950 para NTC 10k)
 */

#include <stdint.h>

/* ── Parámetros del NTC 10kΩ con Beta=3950 ─────────────────────────── */
#define NTC_R_SERIE     10000.0f    /* Resistencia en serie (Ω)         */
#define NTC_R0          10000.0f    /* Resistencia NTC a 25°C (Ω)       */
#define NTC_T0_KELVIN   298.15f     /* 25°C en Kelvin                   */
#define NTC_BETA        3950.0f     /* Coeficiente Beta del NTC         */
#define NTC_VCC         3300.0f     /* Voltaje de alimentación (mV)     */

/* ── API pública ─────────────────────────────────────────────────────── */

/**
 * Convierte el voltaje leído (en mV) a temperatura en grados Celsius.
 * Usa el modelo Beta de Steinhart-Hart.
 *
 * @param voltage_mv  Voltaje en el pin ADC (mV), punto medio del divisor
 * @return            Temperatura en °C
 */
float ntc_voltage_to_celsius(int voltage_mv);

/**
 * Convierte resistencia del NTC a temperatura (°C).
 * Útil si ya calculaste la resistencia por otro método.
 */
float ntc_resistance_to_celsius(float resistance_ohm);

/**
 * Calcula la resistencia del NTC desde el voltaje medido.
 * Despeja R_ntc del divisor de voltaje:
 *   Vout = Vcc * R_ntc / (R_serie + R_ntc)
 *   R_ntc = R_serie * Vout / (Vcc - Vout)
 */
float ntc_voltage_to_resistance(int voltage_mv);

#endif /* NTC_MODULE_H */
