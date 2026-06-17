/*
 * http_server.c
 * Módulo 7 - Servidor Web HTTP e Interfaz Gráfica (Dashboard iOS)
 */

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "sys/param.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "http_server.h"
#include "tasks_common.h"
#include "wifi_app.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h" 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

// Inclusiones de los módulos del proyecto para enlazar las variables
#include "settings.h"
#include "servo_curtain.h"
#include "fan_alarm.h"
#include "schedule.h"

static const char *TAG = "http_server";

// Variables globales del sistema (Definidas en main.c)
extern float g_ambient_temperature;
extern float g_target_temperature;
extern float g_max_temperature;

// Handle del Servidor HTTP
static httpd_handle_t http_server_handle = NULL;

// Task handle para el monitor del servidor
static TaskHandle_t task_http_server_monitor = NULL;

// Queue handle del monitor
static QueueHandle_t http_server_monitor_queue_handle = NULL;

// Embebido de archivos web del SPIFFS / Flash binarios compilados
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t app_css_start[]    asm("_binary_app_css_start");
extern const uint8_t app_css_end[]      asm("_binary_app_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");
extern const uint8_t jquery_start[]     asm("_binary_jquery_3_3_1_min_js_start");
extern const uint8_t jquery_end[]       asm("_binary_jquery_3_3_1_min_js_end");
extern const uint8_t favicon_start[]    asm("_binary_favicon_ico_start");
extern const uint8_t favicon_end[]      asm("_binary_favicon_ico_end");

/**
 * Envía mensajes a la cola del monitor del servidor HTTP
 */
BaseType_t http_server_monitor_send_message(http_server_message_e msgID)
{
    http_server_queue_message_t msg;
    msg.msgID = msgID;
    if (http_server_monitor_queue_handle != NULL) {
        return xQueueSend(http_server_monitor_queue_handle, &msg, portMAX_DELAY);
    }
    return pdFALSE;
}

/* ==========================================================================
   HANDLERS DE LOS ARCHIVOS ESTÁTICOS DE LA INTERFAZ DE USUARIO (HTML/CSS/JS)
   ========================================================================== */

static esp_err_t http_server_index_html_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "index.html solicitado");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t http_server_app_css_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)app_css_start, app_css_end - app_css_start);
    return ESP_OK;
}

static esp_err_t http_server_app_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start);
    return ESP_OK;
}

static esp_err_t http_server_jquery_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_send(req, (const char *)jquery_start, jquery_end - jquery_start);
    return ESP_OK;
}

static esp_err_t http_server_favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_start, favicon_end - favicon_start);
    return ESP_OK;
}

/* ==========================================================================
   NUEVOS HANDLERS JSON - INTERCONEXIÓN DEL MÓDULO 7 (DASHBOARD <-> BACKEND)
   ========================================================================== */

/**
 * Handler para GET /dhtSensorData.json
 * Entrega las lecturas actuales de temperatura, consignas y periféricos.
 */
static esp_err_t http_server_get_dht_sensor_data_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");

    cJSON *root = cJSON_CreateObject();
    // Tu app.js lee directamente estas claves exactas:
    cJSON_AddNumberToObject(root, "temp", g_ambient_temperature);
    cJSON_AddNumberToObject(root, "target_temp", g_target_temperature);
    cJSON_AddNumberToObject(root, "max_temp", g_max_temperature);
    cJSON_AddNumberToObject(root, "servo_pos", servo_get_position());
    cJSON_AddBoolToObject(root, "fan_status", fan_get_status());

    const char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, json_str);

    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * Handler para POST /saveSettings.json
 * Recibe y actualiza los límites de operación térmica (Deseada y Máxima) en NVS.
 */
static esp_err_t http_server_save_settings_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Contenido vacío");
        return ESP_FAIL;
    }

    char *buf = malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error de memoria");
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, buf, total_len);
    if (received <= 0) {
        free(buf);
        return ESP_FAIL;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *target_item = cJSON_GetObjectItem(root, "target_temperature");
        cJSON *max_item = cJSON_GetObjectItem(root, "max_temperature");

        if (target_item) {
            g_target_temperature = (float)target_item->valuedouble;
            settings_set_target_temperature(g_target_temperature);
        }
        if (max_item) {
            g_max_temperature = (float)max_item->valuedouble;
            settings_set_max_temperature(g_max_temperature);
        }
        cJSON_Delete(root);
    }

    free(buf);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"Configuraciones guardadas en NVS\"}");
    return ESP_OK;
}

/**
 * Handler para POST /wifiConnect.json
 * Recibe las credenciales SSID y Password para cambiar a modo Estación (STA).
 */
static esp_err_t http_server_wifi_connect_json_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    char *buf = malloc(total_len + 1);
    if (!buf) return ESP_FAIL;

    httpd_req_recv(req, buf, total_len);
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
        cJSON *pass_item = cJSON_GetObjectItem(root, "password");

        if (ssid_item && pass_item) {
            ESP_LOGI(TAG, "Nuevas credenciales WiFi: SSID=%s", ssid_item->valuestring);
            save_wifi_credentials(ssid_item->valuestring, pass_item->valuestring);
            http_server_monitor_send_message(HTTP_MSG_WIFI_CONNECT_INIT);
        }
        cJSON_Delete(root);
    }

    free(buf);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"connecting\"}");
    return ESP_OK;
}

/**
 * Handler para POST /wifiConnectStatus
 */
static esp_err_t http_server_wifi_connect_status_json_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    // Respuesta simulada de éxito para simplificar flujos de interfaz
    httpd_resp_sendstr(req, "{\"status\":\"connected\"}");
    return ESP_OK;
}

/**
 * Handler para POST /readreg.json (Módulo 5 Horarios)
 * Genera el listado JSON completo mapeando los temporizadores de cortina activos.
 */
static esp_err_t http_server_read_register_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    char *json_buffer = malloc(2048);
    if (!json_buffer) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error de buffer");
        return ESP_FAIL;
    }

    registers_get_json(json_buffer, 2048);
    httpd_resp_sendstr(req, json_buffer);
    
    free(json_buffer);
    return ESP_OK;
}

/**
 * Handler de Actualización Inalámbrica Firmware OTA (POST /OTAupdate)
 */
static esp_err_t http_server_OTA_update_handler(httpd_req_t *req)
{
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No se encontró partición OTA válida para escribir");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falta partición OTA");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Escribiendo en partición: %s", update_partition->label);
    
    int total_len = req->content_len;
    int remaining = total_len;
    char *ota_buff = malloc(1024);
    if (!ota_buff) return ESP_FAIL;

    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        free(ota_buff);
        return ESP_FAIL;
    }

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, ota_buff, MIN(remaining, 1024));
        if (recv_len <= 0) {
            free(ota_buff);
            esp_ota_abort(update_handle);
            return ESP_FAIL;
        }
        esp_ota_write(update_handle, (const void *)ota_buff, recv_len);
        remaining -= recv_len;
    }

    free(ota_buff);
    
    if (esp_ota_end(update_handle) == ESP_OK && esp_ota_set_boot_partition(update_partition) == ESP_OK) {
        ESP_LOGI(TAG, "FIRMWARE OTA COMPLETADO. REINICIANDO...");
        httpd_resp_sendstr(req, "{\"status\":\"success\"}");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Fallo al validar OTA");
    }
    return ESP_OK;
}

/* ==========================================================================
   CONFIGURACIÓN Y MONTAJE DEL SERVIDOR HTTP
   ========================================================================== */

static httpd_handle_t http_server_configure(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Iniciando servidor en puerto: '%d'", config.server_port);
    
    if (httpd_start(&http_server_handle, &config) == ESP_OK) {
        
        // Registro de rutas estáticas
        httpd_uri_t index_html = { .uri = "/", .method = HTTP_GET, .handler = http_server_index_html_handler };
        httpd_register_uri_handler(http_server_handle, &index_html);

        httpd_uri_t app_css = { .uri = "/app.css", .method = HTTP_GET, .handler = http_server_app_css_handler };
        httpd_register_uri_handler(http_server_handle, &app_css);

        httpd_uri_t app_js = { .uri = "/app.js", .method = HTTP_GET, .handler = http_server_app_js_handler };
        httpd_register_uri_handler(http_server_handle, &app_js);

        httpd_uri_t jquery_js = { .uri = "/jquery-3.3.1.min.js", .method = HTTP_GET, .handler = http_server_jquery_handler };
        httpd_register_uri_handler(http_server_handle, &jquery_js);

        httpd_uri_t favicon_ico = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = http_server_favicon_handler };
        httpd_register_uri_handler(http_server_handle, &favicon_ico);

        // Registro de Endpoints Dinámicos (JSON / Operaciones)
        httpd_uri_t dht_sensor_data = { .uri = "/dhtSensorData.json", .method = HTTP_GET, .handler = http_server_get_dht_sensor_data_handler };
        httpd_register_uri_handler(http_server_handle, &dht_sensor_data);

        httpd_uri_t save_settings = { .uri = "/saveSettings.json", .method = HTTP_POST, .handler = http_server_save_settings_handler };
        httpd_register_uri_handler(http_server_handle, &save_settings);

        httpd_uri_t wifi_connect_json = { .uri = "/wifiConnect.json", .method = HTTP_POST, .handler = http_server_wifi_connect_json_handler };
        httpd_register_uri_handler(http_server_handle, &wifi_connect_json);

        httpd_uri_t wifi_connect_status_json = { .uri = "/wifiConnectStatus", .method = HTTP_POST, .handler = http_server_wifi_connect_status_json_handler };
        httpd_register_uri_handler(http_server_handle, &wifi_connect_status_json);

        httpd_uri_t read_range_uri = { .uri = "/readreg.json", .method = HTTP_POST, .handler = http_server_read_register_handler };
        httpd_register_uri_handler(http_server_handle, &read_range_uri);

        httpd_uri_t ota_update_uri = { .uri = "/OTAupdate", .method = HTTP_POST, .handler = http_server_OTA_update_handler };
        httpd_register_uri_handler(http_server_handle, &ota_update_uri);

        return http_server_handle;
    }
    return NULL;
}

static void http_server_monitor_task(void *pvParameters)
{
    http_server_queue_message_t msg;
    for (;;) {
        if (xQueueReceive(http_server_monitor_queue_handle, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.msgID) {
                case HTTP_MSG_WIFI_CONNECT_INIT:
                    ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_INIT detectado");
                    wifi_app_send_message(WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER);
                    break;
                default:
                    break;
            }
        }
    }
}

void http_server_start(void)
{
    if (http_server_handle == NULL) {
        http_server_handle = http_server_configure();
        if (http_server_monitor_queue_handle == NULL) {
            http_server_monitor_queue_handle = xQueueCreate(3, sizeof(http_server_queue_message_t));
        }
        if (task_http_server_monitor == NULL) {
            xTaskCreatePinnedToCore(&http_server_monitor_task, "http_server_monitor", 4096, NULL, MIN_PRIORITY_TASK, &task_http_server_monitor, 0);
        }
    }
}

void http_server_stop(void)
{
    if (http_server_handle) {
        httpd_stop(http_server_handle);
        ESP_LOGI(TAG, "Servidor HTTP detenido exitosamente");
        http_server_handle = NULL;
    }
    if (task_http_server_monitor) {
        vTaskDelete(task_http_server_monitor);
        task_http_server_monitor = NULL;
    }
}