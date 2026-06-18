#ifndef CONTROL_SYSTEM_H
#define CONTROL_SYSTEM_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CONTROL_FAN_GPIO          19
#define CONTROL_SERVO_GPIO        18
#define CONTROL_ALARM_LED_GPIO     2

#define CONTROL_DEFAULT_TEMP_DESIRED_C 28.0f
#define CONTROL_DEFAULT_TEMP_MAX_C     35.0f

typedef enum {
    CONTROL_MODE_MANUAL = 0,
    CONTROL_MODE_AUTO   = 1,
} control_mode_t;

typedef struct {
    float temperature_c;
    int adc_raw;
    int adc_mv;
    bool temperature_valid;

    control_mode_t thermal_mode;
    float desired_temp_c;
    float max_temp_c;
    uint8_t manual_fan_percent;
    uint8_t fan_percent;
    bool alarm_active;

    control_mode_t curtain_mode;
    uint8_t curtain_percent;

    uint8_t rgb_r;
    uint8_t rgb_g;
    uint8_t rgb_b;
    uint8_t rgb_brightness;

    bool time_synchronized;
} control_state_t;

void control_system_init(void);
void control_system_start(void);

esp_err_t control_system_get_state(control_state_t *out_state);
esp_err_t control_system_set_thermal(control_mode_t mode, float desired_temp_c, float max_temp_c, uint8_t manual_fan_percent);
esp_err_t control_system_set_curtain(control_mode_t mode, uint8_t curtain_percent);
esp_err_t control_system_set_rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);
esp_err_t control_system_save_config(void);
esp_err_t control_system_load_config(void);

#endif
