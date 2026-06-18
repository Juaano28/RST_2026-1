/*
 * wifi_app.h
 */

#ifndef MAIN_WIFI_APP_H_
#define MAIN_WIFI_APP_H_

#include "esp_netif.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"

typedef void (*wifi_connected_event_callback_t)(void);

#define WIFI_AP_SSID                "ESP32_VICTOR_new_red"
#define WIFI_AP_PASSWORD            "password123"
#define WIFI_AP_CHANNEL             1
#define WIFI_AP_SSID_HIDDEN         0
#define WIFI_AP_MAX_CONNECTIONS     5
#define WIFI_AP_BEACON_INTERVAL     100
#define WIFI_AP_IP                  "192.168.0.1"
#define WIFI_AP_GATEWAY             "192.168.0.1"
#define WIFI_AP_NETMASK             "255.255.255.0"
#define WIFI_AP_BANDWIDTH           WIFI_BW_HT20
#define WIFI_STA_POWER_SAVE         WIFI_PS_NONE
#define MAX_SSID_LENGTH             32
#define MAX_PASSWORD_LENGTH         64
#define MAX_CONNECTION_RETRIES      5

extern esp_netif_t* esp_netif_sta;
extern esp_netif_t* esp_netif_ap;

typedef enum wifi_app_message
{
    WIFI_APP_MSG_START_HTTP_SERVER = 0,
    WIFI_APP_MSG_CONNECTING_FROM_HTTP_SERVER,
    WIFI_APP_MSG_STA_CONNECTED_GOT_IP,
    WIFI_APP_MSG_USER_REQUESTED_STA_DISCONNECT,
    WIFI_APP_MSG_LOAD_SAVED_CREDENTIALS,
    WIFI_APP_MSG_STA_DISCONNECTED,
    WIFI_APP_MSG_CONNECT_TO_STA,   // <-- NUEVO
} wifi_app_message_e;

typedef struct wifi_app_queue_message
{
    wifi_app_message_e msgID;
} wifi_app_queue_message_t;

BaseType_t wifi_app_send_message(wifi_app_message_e msgID);
void wifi_app_connect_to_sta(const char* ssid, const char* password); // <-- NUEVO
void wifi_app_start(void);
wifi_config_t* wifi_app_get_wifi_config(void);
void wifi_app_set_callback(wifi_connected_event_callback_t cb);
void wifi_app_call_callback(void);
int8_t wifi_app_get_rssi(void);

#endif /* MAIN_WIFI_APP_H_ */