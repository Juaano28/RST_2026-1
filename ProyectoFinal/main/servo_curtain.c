#include "driver/ledc.h"
#include "servo_curtain.h"
#include "esp_log.h"

static const char *TAG = "SERVO";

// Valores calculados para resolución de 14 bits a 50Hz (Período de 20ms)
// 1ms = (1/20) * 16383 = 819.15
// 2ms = (2/20) * 16383 = 1638.3
#define SERVO_MIN_DUTY    819   // Pulse width de 1ms (0% abierto)
#define SERVO_MAX_DUTY    1638  // Pulse width de 2ms (100% abierto)

static bool g_manual_override = false;
static uint8_t g_current_position = 0;

void servo_curtain_init(void) {
    // 1. Configuración del Timer a 50Hz con 14 bits de resolución
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_14_BIT,
        .timer_num        = LEDC_TIMER_2,        // Timer 0 (RGB), Timer 1 (Ventilador), Timer 2 (Servo)
        .freq_hz          = 50,                  // Frecuencia crítica para Servos
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    // 2. Configuración del Canal LEDC asociado al GPIO 7
    ledc_channel_config_t servo_channel = {
        .gpio_num   = SERVO_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_3,            // Canal 0,1,2 (RGB), Canal 4,5 (Ventilador/Alarma), Canal 3 (Servo)
        .timer_sel  = LEDC_TIMER_2,
        .duty       = SERVO_MIN_DUTY,            // Arranca cerrado (0%)
        .hpoint     = 0
    };
    ledc_channel_config(&servo_channel);
    
    ESP_LOGI(TAG, "Módulo de cortina (Servo) inicializado en GPIO %d", SERVO_PWM_GPIO);
}

void servo_set_position(uint8_t open_percent) {
    if (open_percent > 100) open_percent = 100;
    
    g_current_position = open_percent;

    // Mapeo lineal: duty = MIN + (percent / 100) * (MAX - MIN)
    uint32_t duty = SERVO_MIN_DUTY + ((uint32_t)open_percent * (SERVO_MAX_DUTY - SERVO_MIN_DUTY)) / 100;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_3);
    
    ESP_LOGD(TAG, "Posición de cortina ajustada al %d%% (Duty: %lu)", open_percent, duty);
}

void servo_set_manual_override(bool override_active) {
    g_manual_override = override_active;
    ESP_LOGI(TAG, "Modo anulación manual: %s", override_active ? "ACTIVADO" : "DESACTIVADO");
}

bool servo_get_manual_override(void) {
    return g_manual_override;
}