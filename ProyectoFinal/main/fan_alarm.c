#include "driver/ledc.h"
#include "fan_alarm.h"

void fan_alarm_init(void) {
    // 1. Configuración del Timer para el Ventilador y LED (usamos el mismo timer)
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE, // Único modo en C6
        .duty_resolution  = LEDC_TIMER_8_BIT,    // Resolución 0-255
        .timer_num        = LEDC_TIMER_1,        // Timer 0 lo usa el RGB
        .freq_hz          = 5000,                // 5kHz para evitar ruido
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // 2. Configuración del Canal del Ventilador
    ledc_channel_config_t fan_channel = {
        .gpio_num   = FAN_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_4,
        .timer_sel  = LEDC_TIMER_1,
        .duty       = 0
    };
    ledc_channel_config(&fan_channel);

    // 3. Configuración del Canal del LED de Alarma
    ledc_channel_config_t alarm_channel = {
        .gpio_num   = ALARM_LED_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_5,
        .timer_sel  = LEDC_TIMER_1,
        .duty       = 0
    };
    ledc_channel_config(&alarm_channel);
}

void fan_set_speed(uint8_t speed_percent) {
    uint32_t duty = (speed_percent * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_4);
}

void fan_set_alarm_state(bool active) {
    uint32_t duty = active ? 255 : 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5);
}