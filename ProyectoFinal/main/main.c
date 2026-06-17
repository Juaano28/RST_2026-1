#include "nvs_flash.h"
#include "wifi_app.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "adc_module.h"
#include "ntc_module.h"
#include "library_led_c.h" // Módulo 3
#include "fan_alarm.h"      // Módulo 2
#include "servo_curtain.h"  // Módulo 4
#include "schedule.h"   //Modulo 5
#include "settings.h"   //Modulo 5

float g_ambient_temperature = 25.0f;
static const char *TAG = "MAIN";

float g_target_temperature = 25.0f;
float g_max_temperature = 40.0f;

// Prototipos de función avanzados para evitar el error de "implicit declaration"
void sensor_ntc_task_start(void);

// Definición global del RGB
led_rgb_t my_rgb_led = {
    .led_red   = { .gpio_num = LED_RED_GPIO,   .channel = LEDC_CHANNEL_0, .duty_max = DUTY_MAX },
    .led_green = { .gpio_num = LED_GREEN_GPIO, .channel = LEDC_CHANNEL_1, .duty_max = DUTY_MAX },
    .led_blue  = { .gpio_num = LED_BLUE_GPIO,  .channel = LEDC_CHANNEL_2, .duty_max = DUTY_MAX },
    .timer = LEDC_TIMER_0,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_13_BIT
};

// Tarea de muestreo NTC (Módulo 1)
void v_task_ntc_sampler(void *pvParameters)
{
    adc_module_t s_adc;
    int raw = 0, mv = 0;

    // Inicializar hardware ADC (Curve Fitting)
    adc_module_init(&s_adc);

    while (1) {
        // Leer ADC y convertir a Celsius
        adc_module_read_ntc(&s_adc, &raw, &mv);
        g_ambient_temperature = ntc_voltage_to_celsius(mv);

        ESP_LOGI(TAG, "Temp: %.2f °C", g_ambient_temperature);

        // Muestreo cada 1 segundo
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void sensor_ntc_task_start(void)
{
    xTaskCreate(v_task_ntc_sampler, "ntc_task", 4096, NULL, 5, NULL);
}

// Tarea de control térmico (Combina Módulo 2 y 3)
// 2. Modifica la tarea de control térmico existente para incorporar la lógica de la cortina
void v_task_thermal_control(void *pvParameters)
{
    while (1) {
        // Lógica del Módulo 2 usando variables de NVS
        if (g_ambient_temperature > g_max_temperature) {
            fan_set_speed(100);       // Ventilador a tope
            fan_set_alarm_state(true); // Alarma activa
        } else if (g_ambient_temperature > g_target_temperature) {
            fan_set_speed(50);        // Ventilador al 50%
            fan_set_alarm_state(false);
        } else {
            fan_set_speed(0);         // Ventilador apagado
            fan_set_alarm_state(false);
        }

        // Lógica del Módulo 3: LED RGB dinámico
        // Transiciona a rojo basado en g_max_temperature
        set_led_rgb_by_temperature(&my_rgb_led, g_ambient_temperature, 
                                   g_max_temperature - 5.0f, 100.0f, 
                                   g_target_temperature, g_max_temperature - 5.0f,  
                                   0.0f, g_target_temperature);  

        // Lógica del Módulo 4: Servo Cortina automático
        if (!servo_get_manual_override()) {
            if (g_ambient_temperature > g_max_temperature - 8.0f) {
                servo_set_position(0); // Cierra cortinas si calienta
            } else {
                servo_set_position(100); // Abre si está fresco
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}
// Tarea que monitorea el reloj cada minuto
void v_task_scheduler_monitor(void *pvParameters) {
    while (1) {
        // Solo verificamos si tenemos hora sincronizada (puedes añadir un flag)
        check_schedules();
        
        // Esperar 60 segundos para la siguiente revisión
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // NUEVO: Cargar configuraciones de la memoria persistente (Módulo 5)
    registers_init(); // Inicializa los 10 horarios de cortinas
    g_target_temperature = settings_get_target_temperature();
    g_max_temperature = settings_get_max_temperature();
    
    ESP_LOGI(TAG, "Config Loaded -> Target: %.1f C, Max: %.1f C", g_target_temperature, g_max_temperature);

    // Inicializar hardware local
    config_led_rgb(&my_rgb_led);
    fan_alarm_init();
    servo_curtain_init();
    
    init_obtain_time(); // Inicializar SNTP para obtener la hora actual
    
    // Iniciar Tareas
    sensor_ntc_task_start();
    xTaskCreate(v_task_thermal_control, "thermal_ctrl", 4096, NULL, 5, NULL);
    xTaskCreate(v_task_scheduler_monitor, "scheduler_monitor", 4096, NULL, 5, NULL);
    // Iniciar aplicación Wi-Fi
    wifi_app_start();
}