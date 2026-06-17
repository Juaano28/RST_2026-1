#ifndef ADC_MODULE_H
#define ADC_MODULE_H

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <stdbool.h>
#include <stdint.h>

#define ADC_NTC_CHANNEL     ADC_CHANNEL_0   
#define ADC_POT_CHANNEL     ADC_CHANNEL_1   
#define ADC_UNIT            ADC_UNIT_1
#define ADC_ATTEN           ADC_ATTEN_DB_12 
#define ADC_MULTISAMPLING   64              // Ajuste: Mayor estabilidad en lectura

typedef struct {
    adc_oneshot_unit_handle_t handle;
    adc_cali_handle_t         cali_handle_ntc;
    adc_cali_handle_t         cali_handle_pot;
    bool                      cali_enabled;
} adc_module_t;

void adc_module_init(adc_module_t *adc);
void adc_module_read_ntc(adc_module_t *adc, int *out_raw, int *out_mv);
void adc_module_read_pot(adc_module_t *adc, int *out_raw, int *out_mv);
void adc_module_deinit(adc_module_t *adc);

#endif