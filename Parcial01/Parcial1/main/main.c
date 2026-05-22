/*
 * main.c
 * Sistema de temperatura RGB con arquitectura FreeRTOS — versión extendida.
 *
 * ── Cambios respecto a la versión anterior ────────────────────────────
 *
 *   1. Comando TEMP <seg> <unidad>: impresión periódica de temperatura
 *      en Celsius, Fahrenheit o Kelvin desde el terminal UART.
 *
 *   2. Comandos SET_TEMP_R/G/B: configura el rango °C independiente de cada canal
 *      del LED#1. La tarea task_led1_temp lee estos rangos en cada ciclo.
 *
 *   3. Botón (GPIO9) cicla la unidad de temperatura C → F → K → C.
 *      La unidad vive en uart_mod.temp_unit, apuntada desde button_led_module.
 *
 *   4. Potenciómetro (GPIO1) fija un umbral de temperatura 0-100 °C.
 *      LED#2 se enciende en ROJO si la temperatura NTC supera el umbral.
 *      La temperatura NTC se comparte entre tareas mediante s_ntc_celsius_mv
 *      (atómica: entero en mV, sin necesidad de mutex extra para un int32).
 *
 * ── Arquitectura de tareas ────────────────────────────────────────────
 *
 *   task_led1_temp   (prioridad 2):
 *     Lee el NTC cada 2000 ms, actualiza LED#1 con gradiente por temperatura.
 *     Publica ntc_mv en s_shared_ntc_mv para que task_led2_btn la consuma.
 *
 *   task_led2_btn    (prioridad 3):
 *     Lee botón y potenciómetro cada 20 ms.
 *     Lee s_shared_ntc_mv (sin mutex: lectura atómica de int32 en Xtensa/RISC-V).
 *     Convierte mV a °C y compara contra el umbral del POT.
 *
 *   task_uart        (prioridad 1):
 *     Procesa comandos del terminal cada 20 ms.
 *
 * ── Recurso compartido: ADC ──────────────────────────────────────────
 *   Protegido con mutex s_adc_mutex. task_led1_temp toma el mutex,
 *   lee NTC, publica el resultado y lo libera. Las demás tareas
 *   leen NTC/POT también bajo mutex.
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
#include "freertos/semphr.h"
#include "esp_log.h"

#include "library_led_c.h"
#include "adc_module.h"
#include "ntc_module.h"
#include "uart_module.h"
#include "button_led_module.h"

static const char *TAG = "MAIN";

/* ── Períodos de cada tarea ─────────────────────────────────────────── */
#define PERIOD_LED1_MS   2000
#define PERIOD_LED2_MS     20
#define PERIOD_UART_MS     20

/* ── Prioridades ────────────────────────────────────────────────────── */
#define PRIO_LED2_BTN    3
#define PRIO_LED1_TEMP   2
#define PRIO_UART        1

/* ── Tamaños de stack ───────────────────────────────────────────────── */
#define STACK_LED1   3072
#define STACK_LED2   3072
#define STACK_UART   4096

/* ─────────────────────────────────────────────────────────────────────
 * Recursos globales
 * ───────────────────────────────────────────────────────────────────── */
static led_rgb_t           s_led_rgb;
static adc_module_t        s_adc;
static uart_module_t       s_uart_mod;
static button_led_module_t s_btn_led;
static SemaphoreHandle_t   s_adc_mutex;

/*
 * Temperatura compartida entre tareas (en mV del ADC).
 * task_led1_temp escribe, task_led2_btn lee.
 * Una lectura/escritura de int32 es atómica en ESP32-C6 (RISC-V 32 bits),
 * por lo que no se necesita mutex adicional para este valor.
 */
static volatile int32_t s_shared_ntc_mv = 0;

/* ─────────────────────────────────────────────────────────────────────
 * Parámetros de las tareas
 * ───────────────────────────────────────────────────────────────────── */
typedef struct {
    led_rgb_t        *led_rgb;
    adc_module_t     *adc;
    uart_module_t    *uart_mod;      /* para leer rangos de temperatura   */
    SemaphoreHandle_t adc_mutex;
} task_led1_params_t;

typedef struct {
    button_led_module_t *btn_led;
    adc_module_t        *adc;
    SemaphoreHandle_t    adc_mutex;
} task_led2_params_t;

typedef struct {
    uart_module_t    *uart_mod;
    adc_module_t     *adc;
    SemaphoreHandle_t adc_mutex;
} task_uart_params_t;

/* =======================================================================
 * TAREA 1: task_led1_temp
 *
 * Lee NTC → calcula temperatura → actualiza LED#1 con rangos independientes
 * por canal configurados por SET_TEMP_R / SET_TEMP_G / SET_TEMP_B.
 * Publica ntc_mv para que task_led2 calcule el umbral del potenciómetro.
 *
 * Lógica de encendido:
 *   Cada canal (R, G, B) tiene su propio rango [min, max] en °C.
 *   El canal enciende a duty_max si temp está dentro de su rango,
 *   se apaga si está fuera. Los rangos pueden solaparse libremente.
 * ======================================================================= */
static void task_led1_temp(void *pvParameters)
{
    task_led1_params_t *p = (task_led1_params_t *)pvParameters;

    ESP_LOGI(TAG, "task_led1_temp iniciada");

    while (1) {
        int ntc_raw, ntc_mv;

        if (xSemaphoreTake(p->adc_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            adc_module_read_ntc(p->adc, &ntc_raw, &ntc_mv);
            xSemaphoreGive(p->adc_mutex);
        } else {
            ESP_LOGW(TAG, "led1: timeout mutex ADC");
            vTaskDelay(pdMS_TO_TICKS(PERIOD_LED1_MS));
            continue;
        }

        /* Publicar para task_led2 */
        s_shared_ntc_mv = ntc_mv;

        float temp = ntc_voltage_to_celsius(ntc_mv);

        /* Obtener rangos de temperatura configurados por UART */
        float r_min, r_max, g_min, g_max, b_min, b_max;
        uart_get_temp_range(p->uart_mod,
                            &r_min, &r_max,
                            &g_min, &g_max,
                            &b_min, &b_max);

        /*
         * Llama con los rangos independientes de cada canal.
         * Cada color enciende solo si la temperatura está dentro
         * de su propio rango [min, max]. Los rangos pueden solaparse.
         */
        set_led_rgb_by_temperature(p->led_rgb, temp,
                                   r_min, r_max,
                                   g_min, g_max,
                                   b_min, b_max);

        vTaskDelay(pdMS_TO_TICKS(PERIOD_LED1_MS));
    }
}

/* =======================================================================
 * TAREA 2: task_led2_btn
 *
 * Lee botón (cicla unidad C/F/K) y potenciómetro (umbral de temperatura).
 * Compara la temperatura NTC publicada por task_led1 con el umbral.
 * Si temp > umbral → LED#2 rojo. Si no → LED#2 apagado.
 * ======================================================================= */
static void task_led2_btn(void *pvParameters)
{
    task_led2_params_t *p = (task_led2_params_t *)pvParameters;

    ESP_LOGI(TAG, "task_led2_btn iniciada");

    while (1) {
        /* Leer temperatura publicada por task_led1 (atómica, sin mutex) */
        int32_t ntc_mv = s_shared_ntc_mv;
        float ntc_celsius = ntc_voltage_to_celsius((int)ntc_mv);

        if (xSemaphoreTake(p->adc_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            button_led_module_process(p->btn_led, p->adc, ntc_celsius);
            xSemaphoreGive(p->adc_mutex);
        } else {
            ESP_LOGW(TAG, "led2: timeout mutex ADC");
        }

        vTaskDelay(pdMS_TO_TICKS(PERIOD_LED2_MS));
    }
}

/* =======================================================================
 * TAREA 3: task_uart
 * ======================================================================= */
static void task_uart(void *pvParameters)
{
    task_uart_params_t *p = (task_uart_params_t *)pvParameters;

    ESP_LOGI(TAG, "task_uart iniciada");

    while (1) {
        if (xSemaphoreTake(p->adc_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            uart_module_process(p->uart_mod);
            xSemaphoreGive(p->adc_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(PERIOD_UART_MS));
    }
}

/* =======================================================================
 * app_main
 * ======================================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "=== Sistema Temperatura RGB (FreeRTOS) v2 ===");

    /* ── 1. Mutex ADC ─────────────────────────────────────────────── */
    s_adc_mutex = xSemaphoreCreateMutex();
    if (s_adc_mutex == NULL) {
        ESP_LOGE(TAG, "Error creando mutex ADC — sistema detenido");
        return;
    }

    /* ── 2. LED RGB #1 (cátodo común) ────────────────────────────── */
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

    /* ── 3. ADC ───────────────────────────────────────────────────── */
    adc_module_init(&s_adc);

    /* ── 4. UART ──────────────────────────────────────────────────── */
    uart_module_init(&s_uart_mod, &s_led_rgb, &s_adc);

    /* ── 5. LED RGB #2 + botón
     *    Se pasa puntero a temp_unit para que el botón la modifique
     *    directamente y uart_module_process lea el valor actualizado. */
    button_led_module_init(&s_btn_led, &s_uart_mod.temp_unit);

    ESP_LOGI(TAG, "Hardware listo. Creando tareas FreeRTOS...");
    ESP_LOGI(TAG, "LED#1 catodo comun: R=GPIO%d G=GPIO%d B=GPIO%d",
             (int)LED_RED_GPIO, (int)LED_GREEN_GPIO, (int)LED_BLUE_GPIO);
    ESP_LOGI(TAG, "LED#2 anodo comun:  R=GPIO%d G=GPIO%d B=GPIO%d  BTN=GPIO%d",
             (int)BTN_LED_RED_GPIO, (int)BTN_LED_GREEN_GPIO,
             (int)BTN_LED_BLUE_GPIO, (int)BTN_GPIO);
    ESP_LOGI(TAG, "POT umbral: 0-100 C  |  Boton: cicla unidad C/F/K");

    /* ── 6. Parámetros de tareas ─────────────────────────────────── */
    static task_led1_params_t p_led1 = {0};
    p_led1.led_rgb   = &s_led_rgb;
    p_led1.adc       = &s_adc;
    p_led1.uart_mod  = &s_uart_mod;
    p_led1.adc_mutex = NULL;

    static task_led2_params_t p_led2 = {0};
    p_led2.btn_led   = &s_btn_led;
    p_led2.adc       = &s_adc;
    p_led2.adc_mutex = NULL;

    static task_uart_params_t p_uart = {0};
    p_uart.uart_mod  = &s_uart_mod;
    p_uart.adc       = &s_adc;
    p_uart.adc_mutex = NULL;

    p_led1.adc_mutex = s_adc_mutex;
    p_led2.adc_mutex = s_adc_mutex;
    p_uart.adc_mutex = s_adc_mutex;

    /* ── 7. Crear tareas ─────────────────────────────────────────── */
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

    ESP_LOGI(TAG, "3 tareas creadas. Escribe HELP en el terminal.");
    /*
     * app_main retorna: FreeRTOS elimina esta tarea y el scheduler
     * continúa con las tres tareas creadas arriba.
     */
}