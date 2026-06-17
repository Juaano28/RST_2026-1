/*
 * ntc_module.c - Ajustado para control ambiental estable
 */
#include "ntc_module.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "NTC";

float ntc_voltage_to_resistance(int voltage_mv)
{
    float vout = (float)voltage_mv;
    if (vout <= 10.0f)           return 1e9f;  // Protección contra corto a GND
    if (vout >= NTC_VCC - 10.0f) return 0.0f;  // Protección contra circuito abierto

    return (NTC_R_SERIE * vout) / (NTC_VCC - vout);
}

float ntc_resistance_to_celsius(float resistance_ohm)
{
    if (resistance_ohm <= 0.0f || resistance_ohm > 1e6f) {
        return -273.15f; // Valor de error
    }

    float ln_r = logf(resistance_ohm / NTC_R0);
    float inv_T = (1.0f / NTC_T0_KELVIN) + (1.0f / NTC_BETA) * ln_r;
    float T_kelvin = 1.0f / inv_T;
    float temp_celsius = T_kelvin - 273.15f;

    // Validación de rango físico para evitar PWM errático
    if (temp_celsius < -50.0f || temp_celsius > 150.0f) {
        ESP_LOGW(TAG, "Temperatura fuera de rango: %.2f", temp_celsius);
        return -273.15f; 
    }

    return temp_celsius;
}

float ntc_voltage_to_celsius(int voltage_mv)
{
    float resistance = ntc_voltage_to_resistance(voltage_mv);
    return ntc_resistance_to_celsius(resistance);
}