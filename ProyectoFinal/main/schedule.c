#include <stddef.h>          // Para corregir el error: unknown type name 'size_t'
#include "nvs_flash.h"       // Para inicializar y manejar funciones básicas de NVS
#include "nvs.h"             // Para corregir los errores de 'nvs_handle_t', 'nvs_open', 'NVS_READONLY', etc.
#include "esp_err.h"         // Para corregir el error: 'ESP_OK' undeclared
#include "esp_log.h"         // Para corregir el error: implicit declaration of function 'ESP_LOGI'
#include "schedule.h"      // Para corregir el error: 'registers_init' undeclared
#include "wifi_app.h"
#include "settings.h"
#include "servo_curtain.h"
#include <time.h>            // Para funciones de tiempo como time(), localtime_r(), etc.


#define NVS_SETTINGS_NAMESPACE "settings"
#define DEFAULT_TARGET_TEMP    25.0f
#define DEFAULT_MAX_TEMP       40.0f


static const char *TAG = "SCHEDULE_ENGINE";

// Array global modificado desde el servidor HTTP por la interfaz Web
timer_register_t global_registers[MAX_REGISTERS]; 

void check_schedules(void) {
    // Validar primero si el módulo 6 (SNTP) ya cuenta con hora válida de internet
    if (!get_state_time_was_synchronized()) {
        ESP_LOGW(TAG, "Agenda en pausa: Esperando sincronización horaria por SNTP...");
        return;
    }

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo); 

    // Barrido de las 10 ranuras horarias de la Web
    for (int i = 0; i < MAX_REGISTERS; i++) {
        if (global_registers[i].active) {
            
            // Verificar coincidencia matemática exacta de tiempo
            if (timeinfo.tm_hour == global_registers[i].hour && 
                timeinfo.tm_min == global_registers[i].minutes) {
                
                // timeinfo.tm_wday entrega: 0=Domingo, 1=Lunes, ..., 6=Sábado
                if (global_registers[i].days[timeinfo.tm_wday]) {
                    
                    ESP_LOGI(TAG, "CRON TRIGGER: Registro [%d] coincide con hora actual (%02d:%02d). Ejecutando acción en el Servo...", 
                             i, timeinfo.tm_hour, timeinfo.tm_min);
                    
                    // Forzamos la posición configurada en la agenda (ej. 100 abre, 0 cierra)
                    // Si tu struct original no tiene 'action_position', puedes usar por defecto open (100)
                    servo_set_position(100); 
                }
            }
        }
    }
}

// ... El resto de tus funciones (settings_get_target_temperature, etc.) continúa igual
float settings_get_target_temperature(void) {
    nvs_handle_t my_handle;
    float temp = DEFAULT_TARGET_TEMP;
    if (nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READONLY, &my_handle) == ESP_OK) {
        // En NVS v6.0 es seguro leer tipos de datos directos o blobs. 
        // Usaremos un blob de tamaño float para asegurar compatibilidad estricta.
        size_t required_size = sizeof(float);
        nvs_get_blob(my_handle, "target_t", &temp, &required_size);
        nvs_close(my_handle);
    }
    return temp;
}

void settings_set_target_temperature(float temp) {
    nvs_handle_t my_handle;
    if (nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_blob(my_handle, "target_t", &temp, sizeof(float));
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Temperatura Deseada guardada en NVS: %.2f C", temp);
    }
}

float settings_get_max_temperature(void) {
    nvs_handle_t my_handle;
    float temp = DEFAULT_MAX_TEMP;
    if (nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READONLY, &my_handle) == ESP_OK) {
        size_t required_size = sizeof(float);
        nvs_get_blob(my_handle, "max_t", &temp, &required_size);
        nvs_close(my_handle);
    }
    return temp;
}

void settings_set_max_temperature(float temp) {
    nvs_handle_t my_handle;
    if (nvs_open(NVS_SETTINGS_NAMESPACE, NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_blob(my_handle, "max_t", &temp, sizeof(float));
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Temperatura Máxima guardada en NVS: %.2f C", temp);
    }
}