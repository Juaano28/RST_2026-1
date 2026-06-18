#include "http_server.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "control_system.h"
#include "registers.h"
#include "tasks_common.h"
#include "wifi_app.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sys/param.h"

static const char *TAG = "HTTP_SERVER";

static int g_wifi_connect_status = NONE;
static int g_fw_update_status = OTA_UPDATE_PENDING;
static httpd_handle_t http_server_handle = NULL;
static TaskHandle_t task_http_server_monitor = NULL;
static QueueHandle_t http_server_monitor_queue_handle = NULL;
static esp_timer_handle_t fw_update_reset = NULL;
static uint8_t s_led_state = 0;

extern const uint8_t jquery_3_3_1_min_js_start[] asm("_binary_jquery_3_3_1_min_js_start");
extern const uint8_t jquery_3_3_1_min_js_end[]   asm("_binary_jquery_3_3_1_min_js_end");
extern const uint8_t index_html_start[]          asm("_binary_index_html_start");
extern const uint8_t index_html_end[]            asm("_binary_index_html_end");
extern const uint8_t app_css_start[]             asm("_binary_app_css_start");
extern const uint8_t app_css_end[]               asm("_binary_app_css_end");
extern const uint8_t app_js_start[]              asm("_binary_app_js_start");
extern const uint8_t app_js_end[]                asm("_binary_app_js_end");

static esp_err_t send_json_text(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_sendstr(req, json ? json : "{}");
}

static esp_err_t send_json_obj(httpd_req_t *req, cJSON *root)
{
    if (!root) return send_json_text(req, "{\"ok\":false}");
    char *rendered = cJSON_PrintUnformatted(root);
    if (!rendered) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON render failed");
    }
    esp_err_t ret = send_json_text(req, rendered);
    cJSON_free(rendered);
    cJSON_Delete(root);
    return ret;
}

static char *read_req_body(httpd_req_t *req, size_t max_len)
{
    if (req->content_len <= 0 || req->content_len > max_len) {
        return NULL;
    }

    char *buf = calloc(1, req->content_len + 1);
    if (!buf) return NULL;

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(buf);
            return NULL;
        }
        received += ret;
    }
    buf[received] = '\0';
    return buf;
}

void toogle_led(void)
{
    s_led_state = !s_led_state;
    gpio_set_level(BLINK_GPIO, s_led_state);
}

static void add_state_to_json(cJSON *root, const control_state_t *st)
{
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "temperature_c", st->temperature_c);
    cJSON_AddNumberToObject(root, "adc_raw", st->adc_raw);
    cJSON_AddNumberToObject(root, "adc_mv", st->adc_mv);
    cJSON_AddBoolToObject(root, "temperature_valid", st->temperature_valid);
    cJSON_AddStringToObject(root, "thermal_mode", st->thermal_mode == CONTROL_MODE_AUTO ? "auto" : "manual");
    cJSON_AddNumberToObject(root, "desired_temp_c", st->desired_temp_c);
    cJSON_AddNumberToObject(root, "max_temp_c", st->max_temp_c);
    cJSON_AddNumberToObject(root, "manual_fan_percent", st->manual_fan_percent);
    cJSON_AddNumberToObject(root, "fan_percent", st->fan_percent);
    cJSON_AddBoolToObject(root, "alarm_active", st->alarm_active);
    cJSON_AddStringToObject(root, "curtain_mode", st->curtain_mode == CONTROL_MODE_AUTO ? "auto" : "manual");
    cJSON_AddNumberToObject(root, "curtain_percent", st->curtain_percent);
    cJSON_AddNumberToObject(root, "rgb_r", st->rgb_r);
    cJSON_AddNumberToObject(root, "rgb_g", st->rgb_g);
    cJSON_AddNumberToObject(root, "rgb_b", st->rgb_b);
    cJSON_AddNumberToObject(root, "rgb_brightness", st->rgb_brightness);
    cJSON_AddBoolToObject(root, "time_synchronized", st->time_synchronized);
    cJSON_AddNumberToObject(root, "wifi_connect_status", g_wifi_connect_status);
    cJSON_AddNumberToObject(root, "ota_update_status", g_fw_update_status);
}

static esp_err_t http_server_api_state_handler(httpd_req_t *req)
{
    control_state_t st;
    ESP_ERROR_CHECK_WITHOUT_ABORT(control_system_get_state(&st));
    cJSON *root = cJSON_CreateObject();
    add_state_to_json(root, &st);
    return send_json_obj(req, root);
}

static esp_err_t http_server_get_dht_sensor_readings_json_handler(httpd_req_t *req)
{
    control_state_t st;
    control_system_get_state(&st);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "temp", st.temperature_c);
    cJSON_AddNumberToObject(root, "humidity", 0);
    cJSON_AddNumberToObject(root, "adc_raw", st.adc_raw);
    cJSON_AddNumberToObject(root, "adc_mv", st.adc_mv);
    cJSON_AddBoolToObject(root, "valid", st.temperature_valid);
    return send_json_obj(req, root);
}

static control_mode_t parse_mode(const cJSON *item, control_mode_t fallback)
{
    if (cJSON_IsString(item)) {
        if (strcmp(item->valuestring, "auto") == 0) return CONTROL_MODE_AUTO;
        if (strcmp(item->valuestring, "manual") == 0) return CONTROL_MODE_MANUAL;
    }
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item) ? CONTROL_MODE_AUTO : CONTROL_MODE_MANUAL;
    return fallback;
}

static esp_err_t http_server_api_control_handler(httpd_req_t *req)
{
    char *body = read_req_body(req, 2048);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");

    control_state_t current;
    control_system_get_state(&current);

    control_mode_t thermal_mode = parse_mode(cJSON_GetObjectItem(root, "thermal_mode"), current.thermal_mode);
    float desired = cJSON_IsNumber(cJSON_GetObjectItem(root, "desired_temp_c")) ?
                    (float)cJSON_GetObjectItem(root, "desired_temp_c")->valuedouble : current.desired_temp_c;
    float max_temp = cJSON_IsNumber(cJSON_GetObjectItem(root, "max_temp_c")) ?
                     (float)cJSON_GetObjectItem(root, "max_temp_c")->valuedouble : current.max_temp_c;
    uint8_t manual_fan = cJSON_IsNumber(cJSON_GetObjectItem(root, "manual_fan_percent")) ?
                         (uint8_t)cJSON_GetObjectItem(root, "manual_fan_percent")->valueint : current.manual_fan_percent;

    control_mode_t curtain_mode = parse_mode(cJSON_GetObjectItem(root, "curtain_mode"), current.curtain_mode);
    uint8_t curtain = cJSON_IsNumber(cJSON_GetObjectItem(root, "curtain_percent")) ?
                      (uint8_t)cJSON_GetObjectItem(root, "curtain_percent")->valueint : current.curtain_percent;

    uint8_t r = cJSON_IsNumber(cJSON_GetObjectItem(root, "rgb_r")) ? (uint8_t)cJSON_GetObjectItem(root, "rgb_r")->valueint : current.rgb_r;
    uint8_t g = cJSON_IsNumber(cJSON_GetObjectItem(root, "rgb_g")) ? (uint8_t)cJSON_GetObjectItem(root, "rgb_g")->valueint : current.rgb_g;
    uint8_t b = cJSON_IsNumber(cJSON_GetObjectItem(root, "rgb_b")) ? (uint8_t)cJSON_GetObjectItem(root, "rgb_b")->valueint : current.rgb_b;
    uint8_t brightness = cJSON_IsNumber(cJSON_GetObjectItem(root, "rgb_brightness")) ? (uint8_t)cJSON_GetObjectItem(root, "rgb_brightness")->valueint : current.rgb_brightness;

    cJSON_Delete(root);

    esp_err_t err = ESP_OK;
    if ((err = control_system_set_thermal(thermal_mode, desired, max_temp, manual_fan)) != ESP_OK ||
        (err = control_system_set_curtain(curtain_mode, curtain)) != ESP_OK ||
        (err = control_system_set_rgb(r, g, b, brightness)) != ESP_OK) {
        ESP_LOGE(TAG, "Error aplicando control: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Control update failed");
    }

    return http_server_api_state_handler(req);
}

static esp_err_t http_server_read_register_handler(httpd_req_t *req)
{
    char json[1536];
    registers_get_json(json, sizeof(json));
    return send_json_text(req, json);
}

static bool parse_days(cJSON *arr, bool days[7])
{
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) != 7) return false;
    for (int i = 0; i < 7; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (cJSON_IsBool(item)) days[i] = cJSON_IsTrue(item);
        else if (cJSON_IsNumber(item)) days[i] = item->valueint != 0;
        else if (cJSON_IsString(item)) days[i] = strcmp(item->valuestring, "1") == 0 || strcmp(item->valuestring, "true") == 0;
        else return false;
    }
    return true;
}

static esp_err_t http_server_register_change_handler(httpd_req_t *req)
{
    char *body = read_req_body(req, 1024);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");

    cJSON *reg_json = cJSON_GetObjectItem(root, "selectedNumber");
    cJSON *hour_json = cJSON_GetObjectItem(root, "hours");
    cJSON *min_json = cJSON_GetObjectItem(root, "minutes");
    cJSON *curtain_json = cJSON_GetObjectItem(root, "curtain_percent");
    cJSON *days_json = cJSON_GetObjectItem(root, "selectedDays");

    int reg = cJSON_IsString(reg_json) ? atoi(reg_json->valuestring) : (cJSON_IsNumber(reg_json) ? reg_json->valueint : 0);
    int hour = cJSON_IsString(hour_json) ? atoi(hour_json->valuestring) : (cJSON_IsNumber(hour_json) ? hour_json->valueint : -1);
    int min = cJSON_IsString(min_json) ? atoi(min_json->valuestring) : (cJSON_IsNumber(min_json) ? min_json->valueint : -1);
    uint8_t curtain = cJSON_IsNumber(curtain_json) ? (uint8_t)curtain_json->valueint : 100;
    bool days[7] = {0};
    bool ok = parse_days(days_json, days) && registers_update(reg, hour, min, curtain, days);
    cJSON_Delete(root);

    if (!ok) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid register data");
    return http_server_read_register_handler(req);
}

static esp_err_t http_server_register_erase_handler(httpd_req_t *req)
{
    char *body = read_req_body(req, 512);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");

    cJSON *reg_json = cJSON_GetObjectItem(root, "selectedNumber");
    int reg = cJSON_IsString(reg_json) ? atoi(reg_json->valuestring) : (cJSON_IsNumber(reg_json) ? reg_json->valueint : 0);
    cJSON_Delete(root);

    if (!registers_erase(reg)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid register");
    return http_server_read_register_handler(req);
}

static esp_err_t http_server_wifi_connect_json_handler(httpd_req_t *req)
{
    char *body = read_req_body(req, 512);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");

    cJSON *ssid_json = cJSON_GetObjectItem(root, "selectedSSID");
    cJSON *pwd_json = cJSON_GetObjectItem(root, "pwd");
    if (!cJSON_IsString(ssid_json) || !cJSON_IsString(pwd_json) || strlen(ssid_json->valuestring) > 32 || strlen(pwd_json->valuestring) > 64 ||
        (strlen(pwd_json->valuestring) > 0 && strlen(pwd_json->valuestring) < 8)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid WiFi credentials");
    }

    wifi_config_t *cfg = wifi_app_get_wifi_config();
    if (cfg) {
        memset(cfg, 0, sizeof(wifi_config_t));
        strlcpy((char *)cfg->sta.ssid, ssid_json->valuestring, sizeof(cfg->sta.ssid));
        strlcpy((char *)cfg->sta.password, pwd_json->valuestring, sizeof(cfg->sta.password));
    }

    save_wifi_credentials(ssid_json->valuestring, pwd_json->valuestring);
    cJSON_Delete(root);

    g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECTING;
    esp_wifi_disconnect();
    connect_to_wifi();
    return send_json_text(req, "{\"ok\":true,\"wifi_connect_status\":1}");
}


static esp_err_t http_server_ap_config_handler(httpd_req_t *req)
{
    char *body = read_req_body(req, 512);
    if (!body) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");

    cJSON *ssid_json = cJSON_GetObjectItem(root, "ap_ssid");
    cJSON *pwd_json = cJSON_GetObjectItem(root, "ap_password");
    if (!cJSON_IsString(ssid_json) || !cJSON_IsString(pwd_json) || strlen(ssid_json->valuestring) == 0 ||
        strlen(ssid_json->valuestring) > 32 || strlen(pwd_json->valuestring) > 64 ||
        (strlen(pwd_json->valuestring) > 0 && strlen(pwd_json->valuestring) < 8)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid AP config");
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "ap_ssid", ssid_json->valuestring);
        if (err == ESP_OK) err = nvs_set_str(h, "ap_password", pwd_json->valuestring);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }

    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, ssid_json->valuestring, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(ssid_json->valuestring);
    strlcpy((char *)ap_config.ap.password, pwd_json->valuestring, sizeof(ap_config.ap.password));
    ap_config.ap.channel = WIFI_AP_CHANNEL;
    ap_config.ap.ssid_hidden = WIFI_AP_SSID_HIDDEN;
    ap_config.ap.authmode = strlen(pwd_json->valuestring) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = WIFI_AP_MAX_CONNECTIONS;
    ap_config.ap.beacon_interval = WIFI_AP_BEACON_INTERVAL;
    esp_err_t wifi_err = esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config);
    cJSON_Delete(root);

    if (err != ESP_OK || wifi_err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "AP config failed");
    }
    return send_json_text(req, "{\"ok\":true,\"note\":\"AP actualizado y persistido en NVS\"}");
}

static esp_err_t http_server_wifi_connect_status_json_handler(httpd_req_t *req)
{
    char statusJSON[80];
    snprintf(statusJSON, sizeof(statusJSON), "{\"wifi_connect_status\":%d}", g_wifi_connect_status);
    return send_json_text(req, statusJSON);
}

static void http_server_fw_update_reset_timer(void)
{
    if (g_fw_update_status != OTA_UPDATE_SUCCESSFUL) return;
    if (fw_update_reset == NULL) {
        const esp_timer_create_args_t args = {
            .callback = &http_server_fw_update_reset_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "fw_update_reset",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &fw_update_reset));
    }
    ESP_ERROR_CHECK(esp_timer_start_once(fw_update_reset, 8000000));
}

static void http_server_monitor(void *parameter)
{
    http_server_queue_message_t msg;
    for (;;) {
        if (xQueueReceive(http_server_monitor_queue_handle, &msg, portMAX_DELAY)) {
            switch (msg.msgID) {
                case HTTP_MSG_WIFI_CONNECT_INIT:
                    g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECTING;
                    break;
                case HTTP_MSG_WIFI_CONNECT_SUCCESS:
                    g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECT_SUCCESS;
                    break;
                case HTTP_MSG_WIFI_CONNECT_FAIL:
                    g_wifi_connect_status = HTTP_WIFI_STATUS_CONNECT_FAILED;
                    break;
                case HTTP_MSG_OTA_UPDATE_SUCCESSFUL:
                    g_fw_update_status = OTA_UPDATE_SUCCESSFUL;
                    http_server_fw_update_reset_timer();
                    break;
                case HTTP_MSG_OTA_UPDATE_FAILED:
                    g_fw_update_status = OTA_UPDATE_FAILED;
                    break;
                default:
                    break;
            }
        }
    }
}

static esp_err_t http_server_jquery_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    return httpd_resp_send(req, (const char *)jquery_3_3_1_min_js_start, jquery_3_3_1_min_js_end - jquery_3_3_1_min_js_start);
}

static esp_err_t http_server_index_html_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
}

static esp_err_t http_server_app_css_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    return httpd_resp_send(req, (const char *)app_css_start, app_css_end - app_css_start);
}

static esp_err_t http_server_app_js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    return httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start);
}

esp_err_t http_server_OTA_update_handler(httpd_req_t *req)
{
    esp_ota_handle_t ota_handle = 0;
    char ota_buff[1024];
    int content_length = req->content_len;
    int content_received = 0;
    int recv_len = 0;
    bool is_req_body_started = false;
    bool flash_successful = false;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    if (!update_partition || content_length <= 0) {
        http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_FAILED);
        return ESP_FAIL;
    }

    do {
        recv_len = httpd_req_recv(req, ota_buff, MIN(content_length - content_received, sizeof(ota_buff)));
        if (recv_len < 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_FAILED);
            return ESP_FAIL;
        }

        if (!is_req_body_started) {
            char *body_start_p = strstr(ota_buff, "\r\n\r\n");
            if (!body_start_p) {
                http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_FAILED);
                return ESP_FAIL;
            }
            body_start_p += 4;
            int body_part_len = recv_len - (body_start_p - ota_buff);
            ESP_RETURN_ON_ERROR(esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle), TAG, "ota begin");
            ESP_RETURN_ON_ERROR(esp_ota_write(ota_handle, body_start_p, body_part_len), TAG, "ota write first");
            content_received += body_part_len;
            is_req_body_started = true;
        } else {
            ESP_RETURN_ON_ERROR(esp_ota_write(ota_handle, ota_buff, recv_len), TAG, "ota write");
            content_received += recv_len;
        }
    } while (recv_len > 0 && content_received < content_length);

    if (esp_ota_end(ota_handle) == ESP_OK && esp_ota_set_boot_partition(update_partition) == ESP_OK) {
        flash_successful = true;
    }

    http_server_monitor_send_message(flash_successful ? HTTP_MSG_OTA_UPDATE_SUCCESSFUL : HTTP_MSG_OTA_UPDATE_FAILED);
    return send_json_text(req, flash_successful ? "{\"ok\":true}" : "{\"ok\":false}");
}

esp_err_t http_server_OTA_status_handler(httpd_req_t *req)
{
    char otaJSON[160];
    snprintf(otaJSON, sizeof(otaJSON),
             "{\"ota_update_status\":%d,\"compile_time\":\"%s\",\"compile_date\":\"%s\"}",
             g_fw_update_status, __TIME__, __DATE__);
    return send_json_text(req, otaJSON);
}

static esp_err_t http_server_toogle_led_handler(httpd_req_t *req)
{
    toogle_led();
    return send_json_text(req, "{\"ok\":true}");
}

static void register_uri(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *r))
{
    httpd_uri_t h = {.uri = uri, .method = method, .handler = handler, .user_ctx = NULL};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &h));
}

static httpd_handle_t http_server_configure(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = HTTP_SERVER_TASK_CORE_ID;
    config.task_priority = HTTP_SERVER_TASK_PRIORITY;
    config.stack_size = HTTP_SERVER_TASK_STACK_SIZE;
    config.max_uri_handlers = 24;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    config.lru_purge_enable = true;

    http_server_monitor_queue_handle = xQueueCreate(8, sizeof(http_server_queue_message_t));
    configASSERT(http_server_monitor_queue_handle);
    xTaskCreatePinnedToCore(&http_server_monitor, "http_server_monitor", HTTP_SERVER_MONITOR_STACK_SIZE,
                            NULL, HTTP_SERVER_MONITOR_PRIORITY, &task_http_server_monitor, HTTP_SERVER_MONITOR_CORE_ID);

    if (httpd_start(&http_server_handle, &config) == ESP_OK) {
        register_uri(http_server_handle, "/jquery-3.3.1.min.js", HTTP_GET, http_server_jquery_handler);
        register_uri(http_server_handle, "/", HTTP_GET, http_server_index_html_handler);
        register_uri(http_server_handle, "/index.html", HTTP_GET, http_server_index_html_handler);
        register_uri(http_server_handle, "/app.css", HTTP_GET, http_server_app_css_handler);
        register_uri(http_server_handle, "/app.js", HTTP_GET, http_server_app_js_handler);

        register_uri(http_server_handle, "/api/state", HTTP_GET, http_server_api_state_handler);
        register_uri(http_server_handle, "/api/control", HTTP_POST, http_server_api_control_handler);
        register_uri(http_server_handle, "/dhtSensor.json", HTTP_GET, http_server_get_dht_sensor_readings_json_handler);
        register_uri(http_server_handle, "/readreg.json", HTTP_POST, http_server_read_register_handler);
        register_uri(http_server_handle, "/read_regs.json", HTTP_GET, http_server_read_register_handler);
        register_uri(http_server_handle, "/regchange.json", HTTP_POST, http_server_register_change_handler);
        register_uri(http_server_handle, "/regerase.json", HTTP_POST, http_server_register_erase_handler);
        register_uri(http_server_handle, "/wifiConnect.json", HTTP_POST, http_server_wifi_connect_json_handler);
        register_uri(http_server_handle, "/wifiConnectStatus", HTTP_POST, http_server_wifi_connect_status_json_handler);
        register_uri(http_server_handle, "/api/ap_config", HTTP_POST, http_server_ap_config_handler);
        register_uri(http_server_handle, "/OTAupdate", HTTP_POST, http_server_OTA_update_handler);
        register_uri(http_server_handle, "/OTAstatus", HTTP_POST, http_server_OTA_status_handler);
        register_uri(http_server_handle, "/toogle_led.json", HTTP_POST, http_server_toogle_led_handler);

        ESP_LOGI(TAG, "Servidor HTTP iniciado");
        return http_server_handle;
    }
    return NULL;
}

void http_server_start(void)
{
    if (http_server_handle == NULL) {
        http_server_handle = http_server_configure();
    }
}

void http_server_stop(void)
{
    if (http_server_handle) {
        httpd_stop(http_server_handle);
        http_server_handle = NULL;
    }
    if (task_http_server_monitor) {
        vTaskDelete(task_http_server_monitor);
        task_http_server_monitor = NULL;
    }
    if (http_server_monitor_queue_handle) {
        vQueueDelete(http_server_monitor_queue_handle);
        http_server_monitor_queue_handle = NULL;
    }
}

BaseType_t http_server_monitor_send_message(http_server_message_e msgID)
{
    if (!http_server_monitor_queue_handle) return pdFALSE;
    http_server_queue_message_t msg = {.msgID = msgID};
    return xQueueSend(http_server_monitor_queue_handle, &msg, pdMS_TO_TICKS(100));
}

void http_server_fw_update_reset_callback(void *arg)
{
    ESP_LOGI(TAG, "Reinicio por OTA exitosa");
    esp_restart();
}
