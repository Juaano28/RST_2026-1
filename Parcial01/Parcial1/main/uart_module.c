/*
 * uart_module.c
 * Driver UART con parser de comandos extendido.
 *
 * Nuevos comandos respecto a la versión anterior:
 *   TEMP <seg> <unidad>  — impresión periódica (0 = desactivar)
 *   SET_TEMP_R/G/B <min> <max>
 *                        — límites °C para el gradiente del LED#1
 *   READ_UMBRAL          — muestra el umbral actual del LED#2
 *
 * El botón (gestionado en button_led_module) cambia la unidad activa
 * C → F → K → C. La unidad se lee desde uart_mod->temp_unit.
 */

#include "uart_module.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "UART";

/* Cada llamada a uart_module_process ocurre cada ~20ms */
#define CAL_PRINT_TICKS     50   /* 50 × 20ms = 1 s */

/* ─────────────────────────────────────────────────────────────────────
 * Conversión de temperatura
 * ───────────────────────────────────────────────────────────────────── */
static float celsius_to_unit(float c, temp_unit_t unit)
{
    switch (unit) {
        case TEMP_UNIT_F: return c * 9.0f / 5.0f + 32.0f;
        case TEMP_UNIT_K: return c + 273.15f;
        default:          return c;
    }
}

static const char *unit_name(temp_unit_t unit)
{
    switch (unit) {
        case TEMP_UNIT_F: return "F";
        case TEMP_UNIT_K: return "K";
        default:          return "C";
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * uart_send — enviar string por UART
 * ───────────────────────────────────────────────────────────────────── */
static void uart_send(const char *msg)
{
    uart_write_bytes(UART_PORT, msg, strlen(msg));
}

/* ─────────────────────────────────────────────────────────────────────
 * uart_module_init
 * ───────────────────────────────────────────────────────────────────── */
void uart_module_init(uart_module_t *uart_mod, led_rgb_t *led, adc_module_t *adc)
{
    uart_mod->led_rgb                 = led;
    uart_mod->adc                     = adc;
    uart_mod->cmd_len                 = 0;
    uart_mod->cal_mode_active         = false;
    uart_mod->cal_tick_counter        = 0;
    uart_mod->temp_print_active       = false;
    uart_mod->temp_print_interval_ticks = 0;
    uart_mod->temp_print_counter      = 0;
    uart_mod->temp_unit               = TEMP_UNIT_C;

    /* Rangos de temperatura LED#1 por defecto (°C) */
    uart_mod->temp_range_r_min  = 30.0f;
    uart_mod->temp_range_r_max  = 40.0f;
    uart_mod->temp_range_g_min  = 20.0f;
    uart_mod->temp_range_g_max  = 30.0f;
    uart_mod->temp_range_b_min  = 10.0f;
    uart_mod->temp_range_b_max  = 20.0f;

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
 * Getters públicos
 * ───────────────────────────────────────────────────────────────────── */
temp_unit_t uart_get_temp_unit(const uart_module_t *uart_mod)
{
    return uart_mod->temp_unit;
}

void uart_get_temp_range(const uart_module_t *uart_mod,
                          float *r_min, float *r_max,
                          float *g_min, float *g_max,
                          float *b_min, float *b_max)
{
    *r_min = uart_mod->temp_range_r_min;
    *r_max = uart_mod->temp_range_r_max;
    *g_min = uart_mod->temp_range_g_min;
    *g_max = uart_mod->temp_range_g_max;
    *b_min = uart_mod->temp_range_b_min;
    *b_max = uart_mod->temp_range_b_max;
}

/* ─────────────────────────────────────────────────────────────────────
 * cmd_help
 * ───────────────────────────────────────────────────────────────────── */
static void cmd_help(void)
{
    uart_send(
        "\r\n--- Comandos disponibles ---\r\n"
        "  SET_R <min> <max>                       Limites duty LED rojo  (0-100%%)\r\n"
        "  SET_G <min> <max>                       Limites duty LED verde (0-100%%)\r\n"
        "  SET_B <min> <max>                       Limites duty LED azul  (0-100%%)\r\n"
        "  SET_TEMP_R <min> <max>                  Rango temp (C) canal ROJO  LED1\r\n"
        "  SET_TEMP_G <min> <max>                  Rango temp (C) canal VERDE LED1\r\n"
        "  SET_TEMP_B <min> <max>                  Rango temp (C) canal AZUL  LED1\r\n"
        "                                          Ej: SET_TEMP_R 25 35\r\n"
        "  TEMP <seg> <unidad>                     Impresion periodica. Unidades: C F K\r\n"
        "                                          seg=0 desactiva. Ej: TEMP 5 F\r\n"
        "  READ_TEMP                               Temperatura NTC instantanea\r\n"
        "  READ_LED_VALUES                         Duty cycles actuales R, G, B\r\n"
        "  READ_POT                                Voltaje y %% del potenciometro\r\n"
        "  READ_UMBRAL                             Umbral temperatura LED2 (del POT)\r\n"
        "  CAL_MODE                                Modo calibracion continua\r\n"
        "  EXIT                                    Salir de CAL_MODE / TEMP periodico\r\n"
        "  HELP                                    Esta ayuda\r\n"
        "\r\n  Boton fisico: cambia unidad de temperatura C -> F -> K -> C\r\n\r\n"
    );
}

/* ─────────────────────────────────────────────────────────────────────
 * print_cal_line
 * ───────────────────────────────────────────────────────────────────── */
static void print_cal_line(uart_module_t *uart_mod)
{
    adc_module_t *adc = uart_mod->adc;

    int ntc_raw, ntc_mv;
    adc_module_read_ntc(adc, &ntc_raw, &ntc_mv);
    float ntc_r    = ntc_voltage_to_resistance(ntc_mv);
    float ntc_temp = ntc_resistance_to_celsius(ntc_r);

    int pot_raw, pot_mv;
    adc_module_read_pot(adc, &pot_raw, &pot_mv);

    char line[220];
    snprintf(line, sizeof(line),
             "[CAL] NTC: raw=%4d | V=%4dmV | R=%7.1fohm | T=%6.2fC"
             "  ||  POT: raw=%4d | V=%4dmV | %%=%.1f\r\n",
             ntc_raw, ntc_mv, ntc_r, ntc_temp,
             pot_raw, pot_mv,
             (float)pot_raw * 100.0f / 4095.0f);
    uart_send(line);
}

/* ─────────────────────────────────────────────────────────────────────
 * print_temp_line — imprime temperatura en la unidad activa
 * ───────────────────────────────────────────────────────────────────── */
static void print_temp_line(uart_module_t *uart_mod)
{
    int raw, mv;
    adc_module_read_ntc(uart_mod->adc, &raw, &mv);
    float r    = ntc_voltage_to_resistance(mv);
    float tc   = ntc_resistance_to_celsius(r);
    float tval = celsius_to_unit(tc, uart_mod->temp_unit);

    char line[100];
    snprintf(line, sizeof(line),
             "[TEMP] %.2f %s  (raw=%d V=%dmV R=%.1fohm)\r\n",
             tval, unit_name(uart_mod->temp_unit), raw, mv, r);
    uart_send(line);
}

/* ─────────────────────────────────────────────────────────────────────
 * execute_command
 * ───────────────────────────────────────────────────────────────────── */
static void execute_command(uart_module_t *uart_mod, char *cmd)
{
    char resp[200];
    led_rgb_t    *led = uart_mod->led_rgb;
    adc_module_t *adc = uart_mod->adc;

    /* Limpiar \r de Windows */
    int len = strlen(cmd);
    if (len > 0 && cmd[len-1] == '\r') cmd[--len] = '\0';

    /* ── EXIT ────────────────────────────────────────────────────── */
    if (strcmp(cmd, "EXIT") == 0) {
        bool any = false;
        if (uart_mod->cal_mode_active) {
            uart_mod->cal_mode_active = false;
            uart_send("\r\nModo calibracion finalizado.\r\n");
            any = true;
        }
        if (uart_mod->temp_print_active) {
            uart_mod->temp_print_active = false;
            uart_send("Impresion periodica de temperatura desactivada.\r\n");
            any = true;
        }
        if (!any) uart_send("(no hay modo activo)\r\n");

    /* ── CAL_MODE ─────────────────────────────────────────────────── */
    } else if (strcmp(cmd, "CAL_MODE") == 0) {
        uart_mod->cal_mode_active  = true;
        uart_mod->cal_tick_counter = 0;
        uart_send(
            "\r\n=== MODO CALIBRACION ADC ===\r\n"
            "Conecta un multimetro a GPIO0 (NTC) y GPIO1 (POT).\r\n"
            "Escribe EXIT + ENTER para salir.\r\n"
            "------------------------------------------------------\r\n"
            "         NTC: raw     V(mV)   R(ohm)   T(C)"
            "   ||  POT: raw     V(mV)   (%)\r\n"
        );

    /* ── HELP ─────────────────────────────────────────────────────── */
    } else if (strcmp(cmd, "HELP") == 0) {
        cmd_help();

    /* ── TEMP <seg> <unidad> ─────────────────────────────────────── */
    } else if (strncmp(cmd, "TEMP ", 5) == 0) {
        int    seg  = 0;
        char   unit = 'C';
        if (sscanf(cmd + 5, "%d %c", &seg, &unit) >= 1) {
            if (seg <= 0) {
                uart_mod->temp_print_active = false;
                uart_send("Impresion periodica desactivada.\r\n");
            } else {
                /* Normalizar unidad */
                switch (unit) {
                    case 'F': uart_mod->temp_unit = TEMP_UNIT_F; break;
                    case 'K': uart_mod->temp_unit = TEMP_UNIT_K; break;
                    default:  uart_mod->temp_unit = TEMP_UNIT_C; break;
                }
                /* ticks = seg * 1000ms / 20ms por tick */
                uart_mod->temp_print_interval_ticks = (uint32_t)(seg * 50);
                uart_mod->temp_print_counter        = 0;
                uart_mod->temp_print_active         = true;
                snprintf(resp, sizeof(resp),
                         "Temperatura cada %ds en %s. Escribe EXIT para detener.\r\n",
                         seg, unit_name(uart_mod->temp_unit));
                uart_send(resp);
            }
        } else {
            uart_send("Error: uso -> TEMP <segundos> <C|F|K>  Ej: TEMP 5 F\r\n");
        }

    /* ── SET_TEMP_R <min> <max> ─────────────────────────────────────── */
    } else if (strncmp(cmd, "SET_TEMP_R ", 11) == 0) {
        float mn, mx;
        if (sscanf(cmd + 11, "%f %f", &mn, &mx) == 2 && mn < mx) {
            uart_mod->temp_range_r_min = mn;
            uart_mod->temp_range_r_max = mx;
            snprintf(resp, sizeof(resp),
                     "Rango ROJO  : %.1fC - %.1fC\r\n", mn, mx);
            uart_send(resp);
        } else {
            uart_send("Error: SET_TEMP_R <min> <max>  (min < max, en C)\r\n");
        }

    /* ── SET_TEMP_G <min> <max> ─────────────────────────────────────── */
    } else if (strncmp(cmd, "SET_TEMP_G ", 11) == 0) {
        float mn, mx;
        if (sscanf(cmd + 11, "%f %f", &mn, &mx) == 2 && mn < mx) {
            uart_mod->temp_range_g_min = mn;
            uart_mod->temp_range_g_max = mx;
            snprintf(resp, sizeof(resp),
                     "Rango VERDE : %.1fC - %.1fC\r\n", mn, mx);
            uart_send(resp);
        } else {
            uart_send("Error: SET_TEMP_G <min> <max>  (min < max, en C)\r\n");
        }

    /* ── SET_TEMP_B <min> <max> ─────────────────────────────────────── */
    } else if (strncmp(cmd, "SET_TEMP_B ", 11) == 0) {
        float mn, mx;
        if (sscanf(cmd + 11, "%f %f", &mn, &mx) == 2 && mn < mx) {
            uart_mod->temp_range_b_min = mn;
            uart_mod->temp_range_b_max = mx;
            snprintf(resp, sizeof(resp),
                     "Rango AZUL  : %.1fC - %.1fC\r\n", mn, mx);
            uart_send(resp);
        } else {
            uart_send("Error: SET_TEMP_B <min> <max>  (min < max, en C)\r\n");
        }

    /* ── SET_R ────────────────────────────────────────────────────── */
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
            uart_send("Error: SET_R <min> <max>  (0-100, min<max)\r\n");
        }

    /* ── SET_G ────────────────────────────────────────────────────── */
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
            uart_send("Error: SET_G <min> <max>  (0-100, min<max)\r\n");
        }

    /* ── SET_B ────────────────────────────────────────────────────── */
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
            uart_send("Error: SET_B <min> <max>  (0-100, min<max)\r\n");
        }

    /* ── READ_LED_VALUES ─────────────────────────────────────────── */
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
        float tc   = ntc_resistance_to_celsius(r);
        float tval = celsius_to_unit(tc, uart_mod->temp_unit);
        snprintf(resp, sizeof(resp),
                 "NTC: raw=%d  V=%dmV  R=%.1fohm  T=%.2f%s\r\n",
                 raw, mv, r, tval, unit_name(uart_mod->temp_unit));
        uart_send(resp);

    /* ── READ_POT ───────────────────────────────────────────────── */
    } else if (strcmp(cmd, "READ_POT") == 0) {
        int raw, mv;
        adc_module_read_pot(adc, &raw, &mv);
        snprintf(resp, sizeof(resp),
                 "POT: raw=%d  V=%dmV  %.1f%%\r\n",
                 raw, mv, (float)raw * 100.0f / 4095.0f);
        uart_send(resp);

    /* ── READ_UMBRAL ────────────────────────────────────────────── */
    } else if (strcmp(cmd, "READ_UMBRAL") == 0) {
        /* El umbral es calculado en tiempo real desde el POT en
         * button_led_module, pero podemos mostrar el valor actual
         * leyendo el POT y mapeando al rango 0-100 °C. */
        int raw, mv;
        adc_module_read_pot(adc, &raw, &mv);
        float umbral_c = (float)raw * 100.0f / 4095.0f;
        float umbral_u = celsius_to_unit(umbral_c, uart_mod->temp_unit);
        snprintf(resp, sizeof(resp),
                 "Umbral LED2: POT=%.1f%%  ->  %.2f%s (%.2fC)\r\n",
                 (float)raw * 100.0f / 4095.0f,
                 umbral_u, unit_name(uart_mod->temp_unit), umbral_c);
        uart_send(resp);

    /* ── Desconocido ────────────────────────────────────────────── */
    } else if (len > 0) {
        snprintf(resp, sizeof(resp),
                 "Desconocido: '%s'  ->  escribe HELP\r\n", cmd);
        uart_send(resp);
    }

    /* Prompt solo si no estamos en modo continuo */
    if (!uart_mod->cal_mode_active && !uart_mod->temp_print_active) {
        uart_send("> ");
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * uart_module_process — llamar cada ~20ms
 * ───────────────────────────────────────────────────────────────────── */
void uart_module_process(uart_module_t *uart_mod)
{
    /* ── 1. Leer bytes UART ──────────────────────────────────────── */
    uint8_t byte;
    while (uart_read_bytes(UART_PORT, &byte, 1, 0) == 1) {
        uart_write_bytes(UART_PORT, (const char *)&byte, 1);

        if (byte == '\n' || byte == '\r') {
            uart_mod->cmd_buf[uart_mod->cmd_len] = '\0';
            uart_write_bytes(UART_PORT, "\r\n", 2);
            execute_command(uart_mod, uart_mod->cmd_buf);
            uart_mod->cmd_len = 0;
        } else if (byte == 0x7F || byte == '\b') {
            if (uart_mod->cmd_len > 0) {
                uart_mod->cmd_len--;
                uart_write_bytes(UART_PORT, "\b \b", 3);
            }
        } else if (uart_mod->cmd_len < UART_CMD_MAX_LEN - 1) {
            if (byte >= 'a' && byte <= 'z') byte -= 32;
            uart_mod->cmd_buf[uart_mod->cmd_len++] = (char)byte;
        }
    }

    /* ── 2. Modo calibración — imprimir cada ~1 s ────────────────── */
    if (uart_mod->cal_mode_active) {
        uart_mod->cal_tick_counter++;
        if (uart_mod->cal_tick_counter >= CAL_PRINT_TICKS) {
            uart_mod->cal_tick_counter = 0;
            print_cal_line(uart_mod);
        }
    }

    /* ── 3. Impresión periódica de temperatura ────────────────────── */
    if (uart_mod->temp_print_active) {
        uart_mod->temp_print_counter++;
        if (uart_mod->temp_print_counter >= uart_mod->temp_print_interval_ticks) {
            uart_mod->temp_print_counter = 0;
            print_temp_line(uart_mod);
        }
    }
}