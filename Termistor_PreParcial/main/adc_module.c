/*
 * adc_module.c
 * Módulo ADC con calibración Curve Fitting para ESP32-C6.
 *
 * Flujo de datos:
 *   Sensor analógico → GPIO → ADC (0-4095 raw) → Calibración → Voltaje en mV
 *
 * La calibración Curve Fitting usa coeficientes grabados en eFuse durante
 * la fabricación del chip para corregir la no-linealidad del ADC.
 */

#include "adc_module.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ADC";

/* ─────────────────────────────────────────────────────────────────────
 * Función interna: inicializar calibración para un canal
 *
 * El ESP32-C6 usa ADC_CALI_SCHEME_VER_CURVE_FITTING.
 * A diferencia del ESP32 clásico (line fitting), este esquema aplica
 * una curva polinómica de corrección específica para cada chip,
 * grabada en los eFuse durante el test de fábrica.
 *
 * Retorna true si la calibración está disponible, false en caso contrario.
 * ───────────────────────────────────────────────────────────────────── */
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                  adc_atten_t atten, adc_cali_handle_t *out)
{
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = unit,
        .chan     = channel,
        .atten    = atten,              // (12dB) La atenuación afecta la curva de calibración, debe coincidir con la configuración del canal
        .bitwidth = ADC_BITWIDTH_DEFAULT,   
    };

    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, out);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibración Curve Fitting OK — canal %d", channel);
        return true;
    }

    if (ret == ESP_ERR_NOT_SUPPORTED || ret == ESP_ERR_INVALID_VERSION) {
        ESP_LOGW(TAG, "Calibración no disponible en este chip (sin eFuse). "
                       "Usando lectura raw.");
    } else {
        ESP_LOGE(TAG, "Error al crear calibración: %s", esp_err_to_name(ret));
    }
    return false;
}

/* ─────────────────────────────────────────────────────────────────────
 * adc_module_init
 * ───────────────────────────────────────────────────────────────────── */
void adc_module_init(adc_module_t *adc)
{
    /* 1. Crear la unidad ADC en modo oneshot */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,   /* sin modo ultra-low-power */
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc->handle));

    /* 2. Configurar canal NTC */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,   /* 12 bits = rango 0–4095  */
        .atten    = ADC_ATTEN,              /* 12 dB = hasta ~3100 mV  */
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc->handle,
                                                ADC_NTC_CHANNEL, &chan_cfg));

    /* 3. Configurar canal POT (misma atenuación) */
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc->handle,
                                                ADC_POT_CHANNEL, &chan_cfg));

    /* 4. Inicializar calibración para cada canal por separado
     *    (la calibración Curve Fitting es por canal en ESP32-C6) */
    adc->cali_enabled = adc_calibration_init(ADC_UNIT, ADC_NTC_CHANNEL,
                                              ADC_ATTEN, &adc->cali_handle_ntc);
    adc_calibration_init(ADC_UNIT, ADC_POT_CHANNEL,
                         ADC_ATTEN, &adc->cali_handle_pot);

    ESP_LOGI(TAG, "ADC inicializado: NTC=CH%d POT=CH%d ATTEN=12dB multisample=%d",
             ADC_NTC_CHANNEL, ADC_POT_CHANNEL, ADC_MULTISAMPLING);
}

/* ─────────────────────────────────────────────────────────────────────
 * Función interna: leer con promediado (multisampling)
 *
 * Por qué multisampling: el ADC del ESP32-C6 es sensible al ruido.
 * Tomar N muestras y promediarlas reduce el ruido en sqrt(N).
 * Con 16 muestras se reduce el ruido ~4 veces.
 * ───────────────────────────────────────────────────────────────────── */
static void read_channel_averaged(adc_module_t *adc, adc_channel_t channel,
                                   adc_cali_handle_t cali_handle,
                                   int *out_raw, int *out_mv)
{
    int32_t sum = 0;
    int raw;

    for (int i = 0; i < ADC_MULTISAMPLING; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc->handle, channel, &raw));
        sum += raw;
    }

    *out_raw = (int)(sum / ADC_MULTISAMPLING);

    if (adc->cali_enabled && cali_handle != NULL) {
        /* Convertir raw a mV usando la curva de calibración */
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, *out_raw, out_mv));
    } else {
        /* Sin calibración: estimación lineal simple
         * Vref nominal = 1100 mV, atenuación 12dB → máx ~3100 mV
         * mV ≈ raw * 3100 / 4095 */
        *out_mv = (*out_raw * 3100) / 4095;
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * adc_module_read_ntc
 * ───────────────────────────────────────────────────────────────────── */
void adc_module_read_ntc(adc_module_t *adc, int *out_raw, int *out_mv)
{
    read_channel_averaged(adc, ADC_NTC_CHANNEL,
                           adc->cali_handle_ntc, out_raw, out_mv);
}

/* ─────────────────────────────────────────────────────────────────────
 * adc_module_read_pot
 * ───────────────────────────────────────────────────────────────────── */
void adc_module_read_pot(adc_module_t *adc, int *out_raw, int *out_mv)
{
    read_channel_averaged(adc, ADC_POT_CHANNEL,
                           adc->cali_handle_pot, out_raw, out_mv);
}

/* ─────────────────────────────────────────────────────────────────────
 * adc_module_deinit
 * ───────────────────────────────────────────────────────────────────── */
void adc_module_deinit(adc_module_t *adc)
{
    if (adc->cali_handle_ntc) {
        adc_cali_delete_scheme_curve_fitting(adc->cali_handle_ntc);
    }
    if (adc->cali_handle_pot) {
        adc_cali_delete_scheme_curve_fitting(adc->cali_handle_pot);
    }
    adc_oneshot_del_unit(adc->handle);
    ESP_LOGI(TAG, "ADC liberado");
}
