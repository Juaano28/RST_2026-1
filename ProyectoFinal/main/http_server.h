/*
 * http_server.h
 * Módulo 7 - Definiciones del Servidor HTTP
 */

#ifndef MAIN_HTTP_SERVER_H_
#define MAIN_HTTP_SERVER_H_

#include "freertos/FreeRTOS.h"

#define OTA_UPDATE_PENDING       0
#define OTA_UPDATE_SUCCESSFUL    1
#define OTA_UPDATE_FAILED       -1
#define BLINK_GPIO               2

/**
 * Estados de conexión WiFi para el servidor HTTP
 */
typedef enum http_server_wifi_connect_status
{
    NONE = 0,
    HTTP_WIFI_STATUS_CONNECTING,
    HTTP_WIFI_STATUS_CONNECT_FAILED,
    HTTP_WIFI_STATUS_CONNECT_SUCCESS,
} http_server_wifi_connect_status_e;

/**
 * ID de mensajes de eventos para el monitor HTTP
 */
typedef enum http_server_message
{
    HTTP_MSG_WIFI_CONNECT_INIT = 0,
    HTTP_MSG_WIFI_CONNECT_SUCCESS,
    HTTP_MSG_WIFI_CONNECT_FAIL,
    HTTP_MSG_OTA_UPDATE_SUCCESSFUL,
    HTTP_MSG_OTA_UPDATE_FAILED,
} http_server_message_e;

/**
 * Estructura de datos de los mensajes de la cola
 */
typedef struct http_server_queue_message
{
    http_server_message_e msgID;
} http_server_queue_message_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Envía un ID de mensaje a la cola del monitor de ejecución HTTP.
 * @param msgID El ID correspondiente del enum http_server_message_e.
 * @return pdTRUE si se encoló con éxito, de lo contrario pdFALSE.
 */
BaseType_t http_server_monitor_send_message(http_server_message_e msgID);

/**
 * Inicia el servidor web HTTP montando rutas y endpoints.
 */
void http_server_start(void);

/**
 * Detiene el servidor web liberando los descriptores y memoria asignados.
 */
void http_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_HTTP_SERVER_H_ */