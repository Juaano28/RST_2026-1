#include <stdio.h>
#include <string.h>
#include "schedule.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "SCHEDULE";
#define NVS_SCHEDULE_NAMESPACE "schedule"

// Array global en este archivo para mantener el estado de los 10 horarios
static timer_register_t global_registers[MAX_REGISTERS];

void registers_init(void) {
    nvs_handle_t my_handle;
    // Inicializar el array con valores vacíos por defecto
    memset(global_registers, 0, sizeof(global_registers));

    // Intentar leer de la NVS los horarios guardados
    if (nvs_open(NVS_SCHEDULE_NAMESPACE, NVS_READWRITE, &my_handle) == ESP_OK) {
        size_t required_size = sizeof(global_registers);
        esp_err_t err = nvs_get_blob(my_handle, "timer_regs", global_registers, &required_size);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Horarios cargados exitosamente desde NVS.");
        } else {
            ESP_LOGW(TAG, "No se encontraron horarios guardados. Creando por defecto vacíos.");
            nvs_set_blob(my_handle, "timer_regs", global_registers, sizeof(global_registers));
            nvs_commit(my_handle);
        }
        nvs_close(my_handle);
    }
}

void registers_get_json(char *dest_buffer, size_t max_len) {
    // Formatea los horarios guardados en un JSON para tu http_server
    if (dest_buffer == NULL || max_len == 0) return;
    
    int len = snprintf(dest_buffer, max_len, "{\"schedules\":[");
    for (int i = 0; i < MAX_REGISTERS; i++) {
        len += snprintf(dest_buffer + len, max_len - len,
                        "{\"id\":%d,\"active\":%s,\"hour\":%d,\"min\":%d}",
                        i, global_registers[i].active ? "true" : "false",
                        global_registers[i].hour, global_registers[i].minutes);
        if (i < MAX_REGISTERS - 1) {
            len += snprintf(dest_buffer + len, max_len - len, ",");
        }
    }
    snprintf(dest_buffer + len, max_len - len, "]}");
}

bool registers_update(int reg_num, int hour, int minutes, const char* days_str[7]) {
    if (reg_num < 0 || reg_num >= MAX_REGISTERS) return false;

    global_registers[reg_num].hour = hour;
    global_registers[reg_num].minutes = minutes;
    global_registers[reg_num].active = true;

    // Mapear los strings de los días ("1" o "0") al array de booleanos
    for (int i = 0; i < 7; i++) {
        if (days_str[i] != NULL && strcmp(days_str[i], "1") == 0) {
            global_registers[reg_num].days[i] = true;
        } else {
            global_registers[reg_num].days[i] = false;
        }
    }

    // Guardar los cambios actualizados en la NVS
    nvs_handle_t my_handle;
    if (nvs_open(NVS_SCHEDULE_NAMESPACE, NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_blob(my_handle, "timer_regs", global_registers, sizeof(global_registers));
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Horario %d actualizado y guardado en NVS", reg_num);
        return true;
    }
    return false;
}

bool registers_erase(int reg_num) {
    if (reg_num < 0 || reg_num >= MAX_REGISTERS) return false;

    // Resetear el registro seleccionado
    memset(&global_registers[reg_num], 0, sizeof(timer_register_t));

    nvs_handle_t my_handle;
    if (nvs_open(NVS_SCHEDULE_NAMESPACE, NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_blob(my_handle, "timer_regs", global_registers, sizeof(global_registers));
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Horario %d eliminado de NVS", reg_num);
        return true;
    }
    return false;
}