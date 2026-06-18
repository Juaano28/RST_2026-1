/*
 * uart_module.c
 * Driver UART con parser de comandos y modo calibración ADC.
 *
 * Modo calibración (CAL_MODE):
 *   Muestra cada segundo una tabla con los valores de AMBOS canales ADC
 *   (NTC y potenciómetro) para que el usuario pueda comparar el voltaje
 *   reportado por el sistema con un multímetro real.
 *
 *   Mientras el modo está activo, el programa sigue corriendo normalmente
 *   (el LED sigue respondiendo a la temperatura). Solo se añade salida
 *   continua por UART.
 *
 *   Para salir del modo calibración: escribir EXIT y presionar ENTER.
 */

#include "uart_module.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "UART";

/* Cada llamada a uart_module_process ocurre cada ~20ms (desde el loop
 * principal). 50 ticks × 20ms = 1000ms = 1 segundo entre impresiones
 * en modo calibración. */
#define CAL_PRINT_TICKS     50

/* ─────────────────────────────────────────────────────────────────────
 * uart_send — enviar string por UART
 * ───────────────────────────────────────────────────────────────────── */
static void uart_send(const char *msg)
{
    uart_write_bytes(UART_PORT, msg, strlen(msg));
}

/* ─────────────────────────────────────────────────────────────────────
 * uart_module_init
 *
 * Inicializa UART0 a 115200 baudios.
 * UART0 ya está conectado al USB-Serial del nanoESP32-C6, por lo que
 * UART_PIN_NO_CHANGE usa los pines por defecto sin configuración extra.
 * ───────────────────────────────────────────────────────────────────── */
void uart_module_init(uart_module_t *uart_mod, led_rgb_t *led, adc_module_t *adc)
{
    uart_mod->led_rgb          = led;
    uart_mod->adc              = adc;
    uart_mod->cmd_len          = 0;
    uart_mod->cal_mode_active  = false;
    uart_mod->cal_tick_counter = 0;
    memset(uart_mod->cmd_buf, 0, UART_CMD_MAX_LEN);

    uart_config_t uart_cfg = {
        .baud_rate  = UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT,
                                         UART_BUF_SIZE, 0, 0, NULL, 0));

    ESP_LOGI(TAG, "UART0 iniciado @ %d baud", UART_BAUD_RATE);
    uart_send(
        "\r\n========================================\r\n"
        "  Sistema Temperatura RGB — ESP32-C6\r\n"
        "========================================\r\n"
        "Escribe HELP para ver los comandos.\r\n> "
    );
}

/* ─────────────────────────────────────────────────────────────────────
 * cmd_help — mostrar lista de comandos
 * ───────────────────────────────────────────────────────────────────── */
static void cmd_help(void)
{
    uart_send(
        "\r\n--- Comandos disponibles ---\r\n"
        "  SET_R <min> <max>    Limites LED rojo   (0-100%%)\r\n"
        "  SET_G <min> <max>    Limites LED verde  (0-100%%)\r\n"
        "  SET_B <min> <max>    Limites LED azul   (0-100%%)\r\n"
        "  READ_LED_VALUES      Duty cycles actuales de R, G, B\r\n"
        "  READ_TEMP            Temperatura NTC, voltaje y resistencia\r\n"
        "  READ_POT             Voltaje y raw del potenciometro\r\n"
        "  CAL_MODE             Modo calibracion continua (EXIT para salir)\r\n"
        "  HELP                 Esta ayuda\r\n"
        "\r\nEjemplo: SET_R 5 20\r\n\r\n"
    );
}

/* ─────────────────────────────────────────────────────────────────────
 * print_cal_line — imprime UNA línea de datos de calibración
 *
 * Muestra en paralelo NTC y potenciómetro para que el usuario pueda:
 *   1. Medir el voltaje real con un multímetro en cada pin
 *   2. Comparar con el valor reportado aquí
 *   3. Si hay diferencia sistemática, ajustar NTC_VCC o R_SERIE en ntc_module.h
 *
 * Formato:
 *   [CAL] NTC: raw=2100 | Vcal=1690mV | R=10823ohm | T=23.45C  ||
 *         POT: raw=3072 | Vcal=2470mV | %=75.0
 * ───────────────────────────────────────────────────────────────────── */
static void print_cal_line(uart_module_t *uart_mod)
{
    adc_module_t *adc = uart_mod->adc;

    /* Leer NTC */
    int ntc_raw, ntc_mv;
    adc_module_read_ntc(adc, &ntc_raw, &ntc_mv);
    float ntc_r    = ntc_voltage_to_resistance(ntc_mv);
    float ntc_temp = ntc_resistance_to_celsius(ntc_r);

    /* Leer potenciómetro */
    int pot_raw, pot_mv;
    adc_module_read_pot(adc, &pot_raw, &pot_mv);

    char line[200];
    snprintf(line, sizeof(line),
             "[CAL] NTC: raw=%4d | V=%4dmV | R=%7.1fohm | T=%6.2fC"
             "  ||  POT: raw=%4d | V=%4dmV | %%=%.1f\r\n",
             ntc_raw, ntc_mv, ntc_r, ntc_temp,
             pot_raw, pot_mv,
             (float)pot_raw * 100.0f / 4095.0f);
    uart_send(line);
}

/* ─────────────────────────────────────────────────────────────────────
 * execute_command — parsear y ejecutar un comando completo
 * ───────────────────────────────────────────────────────────────────── */
static void execute_command(uart_module_t *uart_mod, char *cmd)
{
    char resp[160];
    led_rgb_t    *led = uart_mod->led_rgb;
    adc_module_t *adc = uart_mod->adc;

    /* Limpiar \r de Windows */
    int len = strlen(cmd);
    if (len > 0 && cmd[len-1] == '\r') cmd[--len] = '\0';

    /* ── EXIT: salir del modo calibración ───────────────────────── */
    if (strcmp(cmd, "EXIT") == 0) {
        if (uart_mod->cal_mode_active) {
            uart_mod->cal_mode_active = false;
            uart_send("\r\nModo calibracion finalizado.\r\n> ");
        } else {
            uart_send("(no hay modo activo que salir)\r\n> ");
        }

    /* ── CAL_MODE: entrar en modo calibración continua ──────────── */
    } else if (strcmp(cmd, "CAL_MODE") == 0) {
        uart_mod->cal_mode_active  = true;
        uart_mod->cal_tick_counter = 0;
        uart_send(
            "\r\n=== MODO CALIBRACION ADC ===\r\n"
            "Conecta un multimetro al pin del NTC (GPIO0) y al POT (GPIO1).\r\n"
            "Compara el voltaje real con el valor 'V=' que se muestra aqui.\r\n"
            "Si difieren sistematicamente, ajusta NTC_VCC en ntc_module.h.\r\n"
            "Escribe EXIT + ENTER para salir.\r\n"
            "-----------------------------------------------------------\r\n"
        );
        /* Imprimir encabezado de columnas */
        uart_send(
            "         NTC: raw     V(mV)   R(ohm)   T(C)   "
            "||  POT: raw     V(mV)   (%)\r\n"
        );

    /* ── HELP ───────────────────────────────────────────────────── */
    } else if (strcmp(cmd, "HELP") == 0) {
        cmd_help();

    /* ── SET_R ──────────────────────────────────────────────────── */
    } else if (strncmp(cmd, "SET_R ", 6) == 0) {
        int mn, mx;
        if (sscanf(cmd + 6, "%d %d", &mn, &mx) == 2
            && mn >= 0 && mx <= 100 && mn < mx) {
            led->led_red.duty_min = percent_to_duty(led, mn);
            led->led_red.duty_max = percent_to_duty(led, mx);
            snprintf(resp, sizeof(resp),
                     "LED Rojo  -> min=%d%% (%lu) max=%d%% (%lu)\r\n",
                     mn, led->led_red.duty_min, mx, led->led_red.duty_max);
            uart_send(resp);
        } else {
            uart_send("Error: uso correcto -> SET_R <min> <max>  (0-100, min<max)\r\n");
        }

    /* ── SET_G ──────────────────────────────────────────────────── */
    } else if (strncmp(cmd, "SET_G ", 6) == 0) {
        int mn, mx;
        if (sscanf(cmd + 6, "%d %d", &mn, &mx) == 2
            && mn >= 0 && mx <= 100 && mn < mx) {
            led->led_green.duty_min = percent_to_duty(led, mn);
            led->led_green.duty_max = percent_to_duty(led, mx);
            snprintf(resp, sizeof(resp),
                     "LED Verde -> min=%d%% (%lu) max=%d%% (%lu)\r\n",
                     mn, led->led_green.duty_min, mx, led->led_green.duty_max);
            uart_send(resp);
        } else {
            uart_send("Error: uso correcto -> SET_G <min> <max>  (0-100, min<max)\r\n");
        }

    /* ── SET_B ──────────────────────────────────────────────────── */
    } else if (strncmp(cmd, "SET_B ", 6) == 0) {
        int mn, mx;
        if (sscanf(cmd + 6, "%d %d", &mn, &mx) == 2
            && mn >= 0 && mx <= 100 && mn < mx) {
            led->led_blue.duty_min = percent_to_duty(led, mn);
            led->led_blue.duty_max = percent_to_duty(led, mx);
            snprintf(resp, sizeof(resp),
                     "LED Azul  -> min=%d%% (%lu) max=%d%% (%lu)\r\n",
                     mn, led->led_blue.duty_min, mx, led->led_blue.duty_max);
            uart_send(resp);
        } else {
            uart_send("Error: uso correcto -> SET_B <min> <max>  (0-100, min<max)\r\n");
        }

    /* ── READ_LED_VALUES ────────────────────────────────────────── */
    } else if (strcmp(cmd, "READ_LED_VALUES") == 0) {
        uint32_t max_duty = (1u << led->duty_resolution) - 1;
        snprintf(resp, sizeof(resp),
                 "R: duty=%lu (%.1f%%)  min=%lu max=%lu\r\n"
                 "G: duty=%lu (%.1f%%)  min=%lu max=%lu\r\n"
                 "B: duty=%lu (%.1f%%)  min=%lu max=%lu\r\n",
                 led->led_red.duty,
                 (float)led->led_red.duty   * 100.0f / max_duty,
                 led->led_red.duty_min,   led->led_red.duty_max,
                 led->led_green.duty,
                 (float)led->led_green.duty * 100.0f / max_duty,
                 led->led_green.duty_min, led->led_green.duty_max,
                 led->led_blue.duty,
                 (float)led->led_blue.duty  * 100.0f / max_duty,
                 led->led_blue.duty_min,  led->led_blue.duty_max);
        uart_send(resp);

    /* ── READ_TEMP ──────────────────────────────────────────────── */
    } else if (strcmp(cmd, "READ_TEMP") == 0) {
        int raw, mv;
        adc_module_read_ntc(adc, &raw, &mv);
        float r    = ntc_voltage_to_resistance(mv);
        float temp = ntc_resistance_to_celsius(r);
        snprintf(resp, sizeof(resp),
                 "NTC: raw=%d  V=%dmV  R=%.1fohm  T=%.2fC\r\n",
                 raw, mv, r, temp);
        uart_send(resp);

    /* ── READ_POT ───────────────────────────────────────────────── */
    } else if (strcmp(cmd, "READ_POT") == 0) {
        int raw, mv;
        adc_module_read_pot(adc, &raw, &mv);
        snprintf(resp, sizeof(resp),
                 "POT: raw=%d  V=%dmV  %.1f%%\r\n",
                 raw, mv, (float)raw * 100.0f / 4095.0f);
        uart_send(resp);

    /* ── Comando desconocido ─────────────────────────────────────── */
    } else if (len > 0) {
        snprintf(resp, sizeof(resp),
                 "Desconocido: '%s'  ->  escribe HELP\r\n", cmd);
        uart_send(resp);
    }

    /* Mostrar prompt solo si NO estamos en modo calibración */
    if (!uart_mod->cal_mode_active) {
        uart_send("> ");
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * uart_module_process
 *
 * Debe llamarse cada ~20ms desde el loop principal.
 * Hace dos cosas:
 *   1. Lee bytes disponibles en el buffer RX y acumula comandos
 *   2. Si el modo calibración está activo, imprime datos cada 1 segundo
 * ───────────────────────────────────────────────────────────────────── */
void uart_module_process(uart_module_t *uart_mod)
{
    /* ── Parte 1: leer bytes del buffer UART ──────────────────────── */
    uint8_t byte;
    while (uart_read_bytes(UART_PORT, &byte, 1, 0) == 1) {

        /* Eco del carácter para que el usuario lo vea */
        uart_write_bytes(UART_PORT, (const char *)&byte, 1);

        if (byte == '\n' || byte == '\r') {
            uart_mod->cmd_buf[uart_mod->cmd_len] = '\0';
            uart_write_bytes(UART_PORT, "\r\n", 2);
            execute_command(uart_mod, uart_mod->cmd_buf);
            uart_mod->cmd_len = 0;

        } else if (byte == 0x7F || byte == '\b') {
            /* Backspace */
            if (uart_mod->cmd_len > 0) {
                uart_mod->cmd_len--;
                uart_write_bytes(UART_PORT, "\b \b", 3);
            }
        } else if (uart_mod->cmd_len < UART_CMD_MAX_LEN - 1) {
            /* Convertir a mayúsculas y acumular */
            if (byte >= 'a' && byte <= 'z') byte -= 32;
            uart_mod->cmd_buf[uart_mod->cmd_len++] = (char)byte;
        }
    }

    /* ── Parte 2: modo calibración — imprimir cada CAL_PRINT_TICKS ── */
    if (uart_mod->cal_mode_active) {
        uart_mod->cal_tick_counter++;
        if (uart_mod->cal_tick_counter >= CAL_PRINT_TICKS) {
            uart_mod->cal_tick_counter = 0;
            print_cal_line(uart_mod);
        }
    }
}