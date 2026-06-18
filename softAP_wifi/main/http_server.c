/*
 * http_server.c
 */

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "sys/param.h"
#include "driver/gpio.h"

#include "http_server.h"
#include "tasks_common.h"
#include "wifi_app.h"
#include "rgb_led.h"

static const char TAG[] = "http_server";
static int g_fw_update_status = OTA_UPDATE_PENDING;
static httpd_handle_t http_server_handle = NULL;
static TaskHandle_t task_http_server_monitor = NULL;
static QueueHandle_t http_server_monitor_queue_handle;
static uint8_t s_led_state = 0;

const esp_timer_create_args_t fw_update_reset_args = {
    .callback = &http_server_fw_update_reset_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "fw_update_reset"
};
esp_timer_handle_t fw_update_reset;

// Archivos embebidos (símbolos generados por CMake)
extern const uint8_t jquery_3_3_1_min_js_start[] asm("_binary_jquery_3_3_1_min_js_start");
extern const uint8_t jquery_3_3_1_min_js_end[]   asm("_binary_jquery_3_3_1_min_js_end");
extern const uint8_t index_html_start[]          asm("_binary_index_html_start");
extern const uint8_t index_html_end[]            asm("_binary_index_html_end");
extern const uint8_t app_css_start[]             asm("_binary_app_css_start");
extern const uint8_t app_css_end[]               asm("_binary_app_css_end");
extern const uint8_t app_js_start[]              asm("_binary_app_js_start");
extern const uint8_t app_js_end[]                asm("_binary_app_js_end");
extern const uint8_t favicon_ico_start[]         asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[]           asm("_binary_favicon_ico_end");

/* Variables externas del wifi_app */
extern int g_wifi_connect_status;
extern void wifi_app_connect_to_sta(const char* ssid, const char* password);

static void http_server_fw_update_reset_timer(void)
{
    if (g_fw_update_status == OTA_UPDATE_SUCCESSFUL) {
        ESP_LOGI(TAG, "FW updated successful starting FW update reset timer");
        ESP_ERROR_CHECK(esp_timer_create(&fw_update_reset_args, &fw_update_reset));
        ESP_ERROR_CHECK(esp_timer_start_once(fw_update_reset, 8000000));
    } else {
        ESP_LOGI(TAG, "FW update unsuccessful");
    }
}

static void http_server_monitor(void *parameter)
{
    http_server_queue_message_t msg;
    for (;;) {
        if (xQueueReceive(http_server_monitor_queue_handle, &msg, portMAX_DELAY)) {
            switch (msg.msgID) {
                case HTTP_MSG_WIFI_CONNECT_INIT:
                    ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_INIT");
                    break;
                case HTTP_MSG_WIFI_CONNECT_SUCCESS:
                    ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_SUCCESS");
                    break;
                case HTTP_MSG_WIFI_CONNECT_FAIL:
                    ESP_LOGI(TAG, "HTTP_MSG_WIFI_CONNECT_FAIL");
                    break;
                case HTTP_MSG_OTA_UPDATE_SUCCESSFUL:
                    ESP_LOGI(TAG, "HTTP_MSG_OTA_UPDATE_SUCCESSFUL");
                    g_fw_update_status = OTA_UPDATE_SUCCESSFUL;
                    http_server_fw_update_reset_timer();
                    break;
                case HTTP_MSG_OTA_UPDATE_FAILED:
                    ESP_LOGI(TAG, "HTTP_MSG_OTA_UPDATE_FAILED");
                    g_fw_update_status = OTA_UPDATE_FAILED;
                    break;
                default:
                    break;
            }
        }
    }
}

/* ============ Handlers de archivos estáticos ============ */
static esp_err_t http_server_jquery_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Jquery requested");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)jquery_3_3_1_min_js_start, jquery_3_3_1_min_js_end - jquery_3_3_1_min_js_start);
    return ESP_OK;
}

static esp_err_t http_server_index_html_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "index.html requested");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t http_server_app_css_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "app.css requested");
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)app_css_start, app_css_end - app_css_start);
    return ESP_OK;
}

static esp_err_t http_server_app_js_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "app.js requested");
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)app_js_start, app_js_end - app_js_start);
    return ESP_OK;
}

static esp_err_t http_server_favicon_ico_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "favicon.ico requested");
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_end - favicon_ico_start);
    return ESP_OK;
}

/* ============ OTA Handlers ============ */
esp_err_t http_server_OTA_update_handler(httpd_req_t *req)
{
    esp_ota_handle_t ota_handle;
    char ota_buff[1024];
    int content_length = req->content_len;
    int content_received = 0;
    int recv_len;
    bool is_req_body_started = false;
    bool flash_successful = false;

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    do {
        if ((recv_len = httpd_req_recv(req, ota_buff, MIN(content_length, sizeof(ota_buff)))) < 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                ESP_LOGI(TAG, "Socket Timeout");
                continue;
            }
            ESP_LOGI(TAG, "OTA other Error %d", recv_len);
            return ESP_FAIL;
        }
        printf("OTA RX: %d of %d\r", content_received, content_length);

        if (!is_req_body_started) {
            is_req_body_started = true;
            char *body_start_p = strstr(ota_buff, "\r\n\r\n") + 4;
            int body_part_len = recv_len - (body_start_p - ota_buff);
            printf("OTA file size: %d\r\n", content_length);

            esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
            if (err != ESP_OK) {
                printf("Error with OTA begin, cancelling OTA\r\n");
                return ESP_FAIL;
            }
            esp_ota_write(ota_handle, body_start_p, body_part_len);
            content_received += body_part_len;
        } else {
            esp_ota_write(ota_handle, ota_buff, recv_len);
            content_received += recv_len;
        }
    } while (recv_len > 0 && content_received < content_length);

    if (esp_ota_end(ota_handle) == ESP_OK) {
        if (esp_ota_set_boot_partition(update_partition) == ESP_OK) {
            flash_successful = true;
        } else {
            ESP_LOGI(TAG, "FLASHED ERROR!!!");
        }
    } else {
        ESP_LOGI(TAG, "esp_ota_end ERROR!!!");
    }

    if (flash_successful) { 
        http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_SUCCESSFUL); 
    } else { 
        http_server_monitor_send_message(HTTP_MSG_OTA_UPDATE_FAILED); 
    }
    return ESP_OK;
}

esp_err_t http_server_OTA_status_handler(httpd_req_t *req)
{
    char otaJSON[100];
    ESP_LOGI(TAG, "OTAstatus requested");
    sprintf(otaJSON, "{\"ota_update_status\":%d,\"compile_time\":\"%s\",\"compile_date\":\"%s\"}", g_fw_update_status, __TIME__, __DATE__);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, otaJSON, strlen(otaJSON));
    return ESP_OK;
}

/* ============ DHT Sensor (Simulación académica) ============ */
static esp_err_t http_server_get_dht_sensor_readings_json_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/dhtSensor.json requested");

    // SIMULACIÓN ACADÉMICA:
    // Generamos variación para que la gráfica del dashboard sea visible.
    // En un proyecto real, reemplaza esto por la lectura de tu sensor DHT22.
    int64_t time_us = esp_timer_get_time();
    float temp = 25.0f + (float)(time_us % 8000000) / 1000000.0f; // oscila ~25-33°C
    float humidity = 50.0f + (float)((time_us / 500000) % 15);   // oscila ~50-65%

    char dhtSensorJSON[100];
    sprintf(dhtSensorJSON, "{\"temp\":\"%.1f\",\"humidity\":\"%.1f\"}", temp, humidity);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, dhtSensorJSON, strlen(dhtSensorJSON));
    return ESP_OK;
}

/* ============ LED Toggle (GPIO 2) ============ */
static esp_err_t http_server_toogle_led_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/toogle_led.json requested");
    s_led_state = !s_led_state;
    gpio_set_level(BLINK_GPIO, s_led_state);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ============ UART Off ============ */
static esp_err_t http_server_toogle_uart_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/uart_off.json requested");
    // Aquí puedes enviar una señal a tu tarea UART para detenerla
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ============ RGB LED Control ============ */
static esp_err_t http_server_rgb_values_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/rgb_vals.json requested");

    size_t len_red_val = 0, len_green_val = 0, len_blue_val = 0;
    char *red_str = NULL, *green_str = NULL, *blue_str = NULL;

    len_red_val = httpd_req_get_hdr_value_len(req, "red_val") + 1;
    if (len_red_val > 1) {
        red_str = malloc(len_red_val);
        if (httpd_req_get_hdr_value_str(req, "red_val", red_str, len_red_val) == ESP_OK) {
            ESP_LOGI(TAG, "red value received: %s", red_str);
        }
    }
    len_green_val = httpd_req_get_hdr_value_len(req, "green_val") + 1;
    if (len_green_val > 1) {
        green_str = malloc(len_green_val);
        if (httpd_req_get_hdr_value_str(req, "green_val", green_str, len_green_val) == ESP_OK) {
            ESP_LOGI(TAG, "green value received: %s", green_str);
        }
    }
    len_blue_val = httpd_req_get_hdr_value_len(req, "blue_val") + 1;
    if (len_blue_val > 1) {
        blue_str = malloc(len_blue_val);
        if (httpd_req_get_hdr_value_str(req, "blue_val", blue_str, len_blue_val) == ESP_OK) {
            ESP_LOGI(TAG, "blue value received: %s", blue_str);
        }
    }

    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);

    if (red_str && green_str && blue_str) {
        rgb_led_set_color(atoi(red_str), atoi(green_str), atoi(blue_str));
    }

    if (red_str) free(red_str);
    if (green_str) free(green_str);
    if (blue_str) free(blue_str);

    return ESP_OK;
}

/* ============ WiFi Connect Handlers NUEVOS ============ */
static esp_err_t http_server_wifi_connect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/wifiConnect.json requested");

    char ssid[32] = {0};
    char pass[64] = {0};

    size_t len_ssid = httpd_req_get_hdr_value_len(req, "my-connect-ssid");
    if (len_ssid > 0 && len_ssid < sizeof(ssid)) {
        httpd_req_get_hdr_value_str(req, "my-connect-ssid", ssid, sizeof(ssid));
    }

    size_t len_pass = httpd_req_get_hdr_value_len(req, "my-connect-pwd");
    if (len_pass > 0 && len_pass < sizeof(pass)) {
        httpd_req_get_hdr_value_str(req, "my-connect-pwd", pass, sizeof(pass));
    }

    ESP_LOGI(TAG, "WiFi connect request SSID=%s", ssid);

    if (strlen(ssid) > 0) {
        wifi_app_connect_to_sta(ssid, pass);
    }

    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t http_server_wifi_connect_status_json_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "/wifiConnectStatus requested");
    char statusJSON[100];
    sprintf(statusJSON, "{\"wifi_connect_status\":%d}", g_wifi_connect_status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, statusJSON, strlen(statusJSON));
    return ESP_OK;
}

/* ============ Configuración y arranque del servidor ============ */
static httpd_handle_t http_server_configure(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    http_server_monitor_queue_handle = xQueueCreate(3, sizeof(http_server_queue_message_t));
    xTaskCreatePinnedToCore(&http_server_monitor, "http_server_monitor", HTTP_SERVER_MONITOR_STACK_SIZE, NULL, HTTP_SERVER_MONITOR_PRIORITY, &task_http_server_monitor, HTTP_SERVER_MONITOR_CORE_ID);

    config.core_id = HTTP_SERVER_TASK_CORE_ID;
    config.task_priority = HTTP_SERVER_TASK_PRIORITY;
    config.stack_size = HTTP_SERVER_TASK_STACK_SIZE;
    config.max_uri_handlers = 20;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    ESP_LOGI(TAG, "Starting server on port: '%d' with task priority: '%d'", config.server_port, config.task_priority);

    if (httpd_start(&http_server_handle, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");

        httpd_uri_t index_html = {
            .uri = "/", .method = HTTP_GET,
            .handler = http_server_index_html_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &index_html);

        httpd_uri_t jquery_js = {
            .uri = "/jquery-3.3.1.min.js", .method = HTTP_GET,
            .handler = http_server_jquery_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &jquery_js);

        httpd_uri_t app_css = {
            .uri = "/app.css", .method = HTTP_GET,
            .handler = http_server_app_css_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &app_css);

        httpd_uri_t app_js = {
            .uri = "/app.js", .method = HTTP_GET,
            .handler = http_server_app_js_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &app_js);

        httpd_uri_t favicon_ico = {
            .uri = "/favicon.ico", .method = HTTP_GET,
            .handler = http_server_favicon_ico_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &favicon_ico);

        httpd_uri_t OTA_update = {
            .uri = "/OTAupdate", .method = HTTP_POST,
            .handler = http_server_OTA_update_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &OTA_update);

        httpd_uri_t OTA_status = {
            .uri = "/OTAstatus", .method = HTTP_POST,
            .handler = http_server_OTA_status_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &OTA_status);

        httpd_uri_t dht_sensor_json = {
            .uri = "/dhtSensor.json", .method = HTTP_GET,
            .handler = http_server_get_dht_sensor_readings_json_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &dht_sensor_json);

        httpd_uri_t toogle_led = {
            .uri = "/toogle_led.json", .method = HTTP_POST,
            .handler = http_server_toogle_led_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &toogle_led);

        // CORRECCIÓN DEL BUG ORIGINAL: uart_off apuntaba al handler de led
        httpd_uri_t toogle_uart = {
            .uri = "/uart_off.json", .method = HTTP_POST,
            .handler = http_server_toogle_uart_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &toogle_uart);

        // RGB re-habilitado
        httpd_uri_t rgb_values = {
            .uri = "/rgb_vals.json", .method = HTTP_POST,
            .handler = http_server_rgb_values_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &rgb_values);

        // NUEVOS: WiFi connection
        httpd_uri_t wifi_connect = {
            .uri = "/wifiConnect.json", .method = HTTP_POST,
            .handler = http_server_wifi_connect_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &wifi_connect);

        httpd_uri_t wifi_connect_status = {
            .uri = "/wifiConnectStatus", .method = HTTP_POST,
            .handler = http_server_wifi_connect_status_json_handler, .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_handle, &wifi_connect_status);

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
        ESP_LOGI(TAG, "http_server_stop: stopping HTTP server");
        http_server_handle = NULL;
    }
    if (task_http_server_monitor) {
        vTaskDelete(task_http_server_monitor);
        ESP_LOGI(TAG, "http_server_stop: stopping HTTP server monitor");
        task_http_server_monitor = NULL;
    }
}

BaseType_t http_server_monitor_send_message(http_server_message_e msgID)
{
    http_server_queue_message_t msg;
    msg.msgID = msgID;
    return xQueueSend(http_server_monitor_queue_handle, &msg, portMAX_DELAY);
}

void http_server_fw_update_reset_callback(void *arg)
{
    ESP_LOGI(TAG, "Timer timed-out, restarting the device");
    esp_restart();
}