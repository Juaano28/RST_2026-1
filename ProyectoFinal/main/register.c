#include "registers.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "REGISTERS";
static const char *NVS_NAMESPACE = "schedules";
static const char *DAY_NAMES[] = {"Lu", "Ma", "Mi", "Ju", "Vi", "Sa", "Do"};

static timer_register_t s_registers[MAX_REGISTERS];
static SemaphoreHandle_t s_reg_mutex;

static void lock_regs(void)
{
    if (s_reg_mutex) xSemaphoreTake(s_reg_mutex, portMAX_DELAY);
}

static void unlock_regs(void)
{
    if (s_reg_mutex) xSemaphoreGive(s_reg_mutex);
}

static bool valid_reg_index(int reg_num)
{
    return reg_num >= 1 && reg_num <= MAX_REGISTERS;
}

static void make_key(int reg_num, char *key, size_t key_len)
{
    snprintf(key, key_len, "reg_%02d", reg_num);
}

void registers_init(void)
{
    if (s_reg_mutex == NULL) {
        s_reg_mutex = xSemaphoreCreateMutex();
        configASSERT(s_reg_mutex);
    }

    lock_regs();
    memset(s_registers, 0, sizeof(s_registers));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No hay namespace NVS de agendas todavía");
        unlock_regs();
        return;
    }

    for (int i = 0; i < MAX_REGISTERS; i++) {
        char key[16];
        make_key(i + 1, key, sizeof(key));
        size_t blob_size = sizeof(timer_register_t);
        err = nvs_get_blob(h, key, &s_registers[i], &blob_size);
        if (err != ESP_OK || blob_size != sizeof(timer_register_t)) {
            memset(&s_registers[i], 0, sizeof(timer_register_t));
            s_registers[i].active = false;
        }
    }
    nvs_close(h);
    unlock_regs();
    ESP_LOGI(TAG, "Agenda cargada desde NVS");
}

void registers_get_json(char *dest_buffer, size_t max_len)
{
    if (!dest_buffer || max_len == 0) return;

    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_AddArrayToObject(root, "registers");

    lock_regs();
    for (int i = 0; i < MAX_REGISTERS; i++) {
        const timer_register_t *reg = &s_registers[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", i + 1);
        cJSON_AddBoolToObject(item, "active", reg->active);
        cJSON_AddNumberToObject(item, "hour", reg->hour);
        cJSON_AddNumberToObject(item, "minutes", reg->minutes);
        cJSON_AddNumberToObject(item, "curtain_percent", reg->curtain_percent);

        cJSON *days = cJSON_AddArrayToObject(item, "days");
        char summary[48] = "";
        bool first = true;
        for (int d = 0; d < 7; d++) {
            cJSON_AddItemToArray(days, cJSON_CreateBool(reg->days[d]));
            if (reg->active && reg->days[d]) {
                if (!first) strlcat(summary, ",", sizeof(summary));
                strlcat(summary, DAY_NAMES[d], sizeof(summary));
                first = false;
            }
        }
        if (!reg->active) {
            cJSON_AddStringToObject(item, "label", "--");
        } else {
            if (summary[0] == '\0') strlcpy(summary, "Sin días", sizeof(summary));
            char label[96];
            snprintf(label, sizeof(label), "%02u:%02u (%s) -> %u%%", reg->hour, reg->minutes, summary, reg->curtain_percent);
            cJSON_AddStringToObject(item, "label", label);
        }
        cJSON_AddItemToArray(items, item);
    }
    unlock_regs();

    char *rendered = cJSON_PrintUnformatted(root);
    if (rendered) {
        strlcpy(dest_buffer, rendered, max_len);
        cJSON_free(rendered);
    } else {
        strlcpy(dest_buffer, "{\"registers\":[]}", max_len);
    }
    cJSON_Delete(root);
}

bool registers_update(int reg_num, int hour, int minutes, uint8_t curtain_percent, const bool days[7])
{
    if (!valid_reg_index(reg_num) || hour < 0 || hour > 23 || minutes < 0 || minutes > 59 || !days) {
        return false;
    }
    if (curtain_percent > 100) curtain_percent = 100;
    int idx = reg_num - 1;

    timer_register_t new_reg = {
        .hour = (uint8_t)hour,
        .minutes = (uint8_t)minutes,
        .curtain_percent = curtain_percent,
        .active = true,
    };
    memcpy(new_reg.days, days, sizeof(new_reg.days));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return false;

    char key[16];
    make_key(reg_num, key, sizeof(key));
    err = nvs_set_blob(h, key, &new_reg, sizeof(new_reg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return false;

    lock_regs();
    s_registers[idx] = new_reg;
    unlock_regs();

    ESP_LOGI(TAG, "Registro %d actualizado: %02d:%02d -> %u%%", reg_num, hour, minutes, curtain_percent);
    return true;
}

bool registers_erase(int reg_num)
{
    if (!valid_reg_index(reg_num)) return false;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        char key[16];
        make_key(reg_num, key, sizeof(key));
        nvs_erase_key(h, key);
        nvs_commit(h);
        nvs_close(h);
    }

    lock_regs();
    memset(&s_registers[reg_num - 1], 0, sizeof(timer_register_t));
    s_registers[reg_num - 1].active = false;
    unlock_regs();
    ESP_LOGI(TAG, "Registro %d eliminado", reg_num);
    return true;
}

void registers_copy_all(timer_register_t *dest, size_t max_items)
{
    if (!dest || max_items == 0) return;
    size_t n = max_items < MAX_REGISTERS ? max_items : MAX_REGISTERS;
    lock_regs();
    memcpy(dest, s_registers, n * sizeof(timer_register_t));
    unlock_regs();
}
