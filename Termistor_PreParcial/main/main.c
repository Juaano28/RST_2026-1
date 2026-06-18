/*
 * main.c
 * Sistema de temperatura RGB con arquitectura FreeRTOS.
 *
 * ── Arquitectura de tareas ────────────────────────────────────────────
 *
 *   task_led1_temp   (prioridad 2):
 *     Lee el NTC por ADC cada 2000 ms y actualiza el LED#1 (cátodo común)
 *     con el gradiente de color según temperatura.
 *
 *   task_led2_btn    (prioridad 3 — mayor, respuesta rápida al botón):
 *     Lee el botón y el potenciómetro cada 20 ms.
 *     Gestiona la lógica de estados acumulativos del LED#2 (ánodo común).
 *
 *   task_uart        (prioridad 1 — menor, no es crítico en tiempo):
 *     Procesa comandos del terminal serie cada 20 ms.
 *     Comandos: SET_R/G/B, READ_TEMP, READ_POT, READ_LED_VALUES,
 *               CAL_MODE, HELP.
 *
 * ── Recurso compartido: ADC ──────────────────────────────────────────
 *   El módulo ADC es usado por task_led1_temp (canal NTC) y por
 *   task_led2_btn (canal POT), y también por task_uart (READ_TEMP, etc).
 *   Se protege con un mutex para evitar lecturas simultáneas que
 *   corrompan los resultados.
 *
 * ── ¿Por qué FreeRTOS en lugar de un loop? ───────────────────────────
 *   Con un loop único, si una operación tarda (ej: 16 lecturas ADC para
 *   multisampling × 3 canales = 48 lecturas), el botón puede no
 *   detectarse a tiempo. Con tareas independientes cada una corre a su
 *   propia velocidad y prioridad, y el scheduler garantiza que la tarea
 *   más urgente siempre consigue CPU cuando la necesita.
 *
 * Hardware:
 *   LED RGB #1 (cátodo común):  R=GPIO2  G=GPIO4  B=GPIO5
 *   LED RGB #2 (ánodo común):   R=GPIO10 G=GPIO11 B=GPIO15
 *   Botón   → GPIO9   (pull-up interno, activo en bajo)
 *   NTC     → GPIO0   (ADC_CHANNEL_0)
 *   POT     → GPIO1   (ADC_CHANNEL_1)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"     /* SemaphoreHandle_t, mutex */
#include "esp_log.h"

#include "library_led_c.h"
#include "adc_module.h"
#include "ntc_module.h"
#include "uart_module.h"
#include "button_led_module.h"

static const char *TAG = "MAIN";

/* ── Rangos de temperatura LED#1 ────────────────────────────────────── */
#define TEMP_MIN    20.0f
#define TEMP_MAX    35.0f

/* ── Períodos de cada tarea ─────────────────────────────────────────── */
#define PERIOD_LED1_MS   2000   /* actualizar LED#1 cada 2 s    */
#define PERIOD_LED2_MS     20   /* leer botón/POT  cada 20 ms   */
#define PERIOD_UART_MS     20   /* leer UART       cada 20 ms   */

/* ── Prioridades de tareas ──────────────────────────────────────────── */
#define PRIO_LED2_BTN    3   /* mayor: respuesta ágil al botón           */
#define PRIO_LED1_TEMP   2   /* media: temperatura cambia lento          */
#define PRIO_UART        1   /* menor: comandos no son críticos en tiempo */

/* ── Tamaños de stack (en palabras de 32 bits) ──────────────────────── */
#define STACK_LED1   3072
#define STACK_LED2   3072
#define STACK_UART   4096   /* más grande: sscanf y snprintf consumen más */

/* ─────────────────────────────────────────────────────────────────────
 * Recursos globales compartidos entre tareas
 *
 * Se declaran static para que solo sean visibles en este archivo.
 * Las tareas reciben punteros a estos recursos a través de sus
 * parámetros (pvParameters).
 * ───────────────────────────────────────────────────────────────────── */
static led_rgb_t          s_led_rgb;
static adc_module_t       s_adc;
static uart_module_t      s_uart_mod;
static button_led_module_t s_btn_led;
static SemaphoreHandle_t  s_adc_mutex;   /* protege s_adc entre tareas */

/* ─────────────────────────────────────────────────────────────────────
 * Estructura de parámetros para task_led1_temp
 * ───────────────────────────────────────────────────────────────────── */
typedef struct {
    led_rgb_t        *led_rgb;
    adc_module_t     *adc;
    SemaphoreHandle_t adc_mutex;
} task_led1_params_t;

/* ─────────────────────────────────────────────────────────────────────
 * Estructura de parámetros para task_led2_btn
 * ───────────────────────────────────────────────────────────────────── */
typedef struct {
    button_led_module_t *btn_led;
    adc_module_t        *adc;
    SemaphoreHandle_t    adc_mutex;
} task_led2_params_t;

/* ─────────────────────────────────────────────────────────────────────
 * Estructura de parámetros para task_uart
 * ───────────────────────────────────────────────────────────────────── */
typedef struct {
    uart_module_t    *uart_mod;
    adc_module_t     *adc;
    SemaphoreHandle_t adc_mutex;
} task_uart_params_t;

/* =======================================================================
 * TAREA 1: task_led1_temp
 * Lee el NTC y actualiza el LED#1 (temperatura) cada PERIOD_LED1_MS ms.
 *
 * Patrón de uso del mutex:
 *   xSemaphoreTake(mutex, timeout) → tomar el mutex (bloquear si ocupado)
 *   ... usar el ADC ...
 *   xSemaphoreGive(mutex)           → liberar el mutex
 * ======================================================================= */
static void task_led1_temp(void *pvParameters)
{
    task_led1_params_t *p = (task_led1_params_t *)pvParameters;

    ESP_LOGI(TAG, "task_led1_temp iniciada");

    while (1) {
        int ntc_raw, ntc_mv;

        /* Tomar el mutex antes de usar el ADC compartido */
        if (xSemaphoreTake(p->adc_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            adc_module_read_ntc(p->adc, &ntc_raw, &ntc_mv);
            xSemaphoreGive(p->adc_mutex);   /* liberar inmediatamente */
        } else {
            /* Si no consigue el mutex en 100ms, saltar esta lectura */
            ESP_LOGW(TAG, "led1: timeout esperando mutex ADC");
            vTaskDelay(pdMS_TO_TICKS(PERIOD_LED1_MS));
            continue;
        }

        float temp = ntc_voltage_to_celsius(ntc_mv);
        set_led_rgb_by_temperature(p->led_rgb, temp, TEMP_MIN, TEMP_MAX);

        /*
         * Log desactivado en producción — satura UART0.
         * Descomentar solo para debug con idf.py monitor:
         *
         * ESP_LOGI(TAG, "LED1 T=%.2fC V=%dmV", temp, ntc_mv);
         */

        /* Dormirse el período completo: cede CPU a otras tareas */
        vTaskDelay(pdMS_TO_TICKS(PERIOD_LED1_MS));
    }
}

/* =======================================================================
 * TAREA 2: task_led2_btn
 * Lee el botón y el potenciómetro cada 20 ms, actualiza LED#2.
 * Tiene la prioridad más alta para responder ágilmente al botón.
 * ======================================================================= */
static void task_led2_btn(void *pvParameters)
{
    task_led2_params_t *p = (task_led2_params_t *)pvParameters;

    ESP_LOGI(TAG, "task_led2_btn iniciada");

    while (1) {
        /* Tomar mutex para leer el POT por ADC */
        if (xSemaphoreTake(p->adc_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            button_led_module_process(p->btn_led, p->adc);
            xSemaphoreGive(p->adc_mutex);
        } else {
            ESP_LOGW(TAG, "led2: timeout esperando mutex ADC");
        }

        vTaskDelay(pdMS_TO_TICKS(PERIOD_LED2_MS));
    }
}

/* =======================================================================
 * TAREA 3: task_uart
 * Procesa comandos del terminal serie cada 20 ms.
 * Prioridad más baja: los comandos no son críticos en tiempo real.
 * ======================================================================= */
static void task_uart(void *pvParameters)
{
    task_uart_params_t *p = (task_uart_params_t *)pvParameters;

    ESP_LOGI(TAG, "task_uart iniciada");

    while (1) {
        /*
         * uart_module_process ya es no bloqueante internamente
         * (usa uart_read_bytes con timeout=0).
         * El mutex solo se necesita cuando process internamente
         * lee el ADC (READ_TEMP, READ_POT, CAL_MODE).
         * Como uart_module no tiene acceso directo al mutex,
         * tomamos el mutex durante todo el process — es rápido.
         */
        if (xSemaphoreTake(p->adc_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            uart_module_process(p->uart_mod);
            xSemaphoreGive(p->adc_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(PERIOD_UART_MS));
    }
}

/* =======================================================================
 * app_main — inicialización y arranque de tareas
 *
 * app_main en FreeRTOS corre como una tarea temporal de prioridad 1.
 * Después de crear las tareas y lanzarlas, termina (return) y FreeRTOS
 * la elimina automáticamente. Las tareas creadas siguen corriendo.
 * ======================================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "=== Sistema Temperatura RGB (FreeRTOS) ===");

    /* ── 1. Mutex ADC — debe crearse ANTES de las tareas ────────────── */
    s_adc_mutex = xSemaphoreCreateMutex();
    if (s_adc_mutex == NULL) {
        ESP_LOGE(TAG, "Error creando mutex ADC — sistema detenido");
        return;
    }

    /* ── 2. LED RGB #1 (cátodo común, temperatura) ───────────────────── */
    s_led_rgb = (led_rgb_t){
        .led_red   = { .gpio_num = LED_RED_GPIO,   .channel = LEDC_CHANNEL_0,
                       .duty = 0, .duty_min = 0, .duty_max = DUTY_MAX },
        .led_green = { .gpio_num = LED_GREEN_GPIO,  .channel = LEDC_CHANNEL_1,
                       .duty = 0, .duty_min = 0, .duty_max = DUTY_MAX },
        .led_blue  = { .gpio_num = LED_BLUE_GPIO,   .channel = LEDC_CHANNEL_2,
                       .duty = 0, .duty_min = 0, .duty_max = DUTY_MAX },
        .timer           = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .frequency       = 1000,
        .speed_mode      = LEDC_LOW_SPEED_MODE,
    };
    config_led_rgb(&s_led_rgb);

    /* ── 3. ADC ──────────────────────────────────────────────────────── */
    adc_module_init(&s_adc);

    /* ── 4. UART ─────────────────────────────────────────────────────── */
    uart_module_init(&s_uart_mod, &s_led_rgb, &s_adc);

    /* ── 5. LED RGB #2 (ánodo común, botón + POT) ───────────────────── */
    button_led_module_init(&s_btn_led);

    ESP_LOGI(TAG, "Hardware listo. Creando tareas FreeRTOS...");
    ESP_LOGI(TAG, "LED#1 catodo comun: R=GPIO%d G=GPIO%d B=GPIO%d",
             (int)LED_RED_GPIO, (int)LED_GREEN_GPIO, (int)LED_BLUE_GPIO);
    ESP_LOGI(TAG, "LED#2 anodo comun:  R=GPIO%d G=GPIO%d B=GPIO%d  BTN=GPIO%d",
             (int)BTN_LED_RED_GPIO, (int)BTN_LED_GREEN_GPIO,
             (int)BTN_LED_BLUE_GPIO, (int)BTN_GPIO);

    /* ── 6. Parámetros de cada tarea ─────────────────────────────────── */
    static task_led1_params_t p_led1 = {0};
    p_led1.led_rgb    = &s_led_rgb;
    p_led1.adc        = &s_adc;
    p_led1.adc_mutex  = NULL;   /* se asigna justo abajo */

    static task_led2_params_t p_led2 = {0};
    p_led2.btn_led    = &s_btn_led;
    p_led2.adc        = &s_adc;
    p_led2.adc_mutex  = NULL;

    static task_uart_params_t p_uart = {0};
    p_uart.uart_mod   = &s_uart_mod;
    p_uart.adc        = &s_adc;
    p_uart.adc_mutex  = NULL;

    /* Asignar el mutex (no podía asignarse en el inicializador estático) */
    p_led1.adc_mutex = s_adc_mutex;
    p_led2.adc_mutex = s_adc_mutex;
    p_uart.adc_mutex = s_adc_mutex;

    /* ── 7. Crear tareas FreeRTOS ─────────────────────────────────────
     *
     * xTaskCreate(función, nombre, stack_words, parámetros, prioridad, handle)
     *
     * - nombre:        string para identificar en el monitor de tareas
     * - stack_words:   palabras de 32 bits (no bytes) del stack privado
     * - parámetros:    puntero void* que se pasa a pvParameters
     * - prioridad:     número mayor = más urgente
     * - handle:        NULL si no necesitas controlar la tarea después
     * ──────────────────────────────────────────────────────────────── */
    BaseType_t ret;

    ret = xTaskCreate(task_led1_temp, "led1_temp",
                      STACK_LED1, &p_led1, PRIO_LED1_TEMP, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando task_led1_temp");
        return;
    }

    ret = xTaskCreate(task_led2_btn, "led2_btn",
                      STACK_LED2, &p_led2, PRIO_LED2_BTN, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando task_led2_btn");
        return;
    }

    ret = xTaskCreate(task_uart, "uart_cmd",
                      STACK_UART, &p_uart, PRIO_UART, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Error creando task_uart");
        return;
    }

    ESP_LOGI(TAG, "3 tareas creadas. app_main finaliza — FreeRTOS toma el control.");
    ESP_LOGI(TAG, "Escribe HELP en el terminal para ver comandos.");

    /*
     * app_main retorna aquí.
     * FreeRTOS elimina esta tarea y el scheduler continúa con las
     * tres tareas creadas arriba, que corren indefinidamente.
     */
}