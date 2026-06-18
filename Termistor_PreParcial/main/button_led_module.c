/*
 * button_led_module.c
 * Control del LED RGB #2 (ánodo común) con lógica de estados acumulativos.
 *
 * ── Ánodo común — cómo funciona la inversión ─────────────────────────
 *
 *   En un LED de cátodo común (LED #1):
 *     GPIO en HIGH → corriente fluye → LED enciende
 *     duty_ledc alto = más brillo
 *
 *   En un LED de ánodo común (LED #2):
 *     El ánodo (+) está conectado a VCC (3.3V).
 *     El GPIO controla el cátodo (-).
 *     GPIO en LOW  → diferencia de potencial → corriente fluye → LED enciende
 *     GPIO en HIGH → mismo potencial que ánodo → no hay corriente → apagado
 *
 *   Por tanto: duty_ledc BAJO = LED encendido, duty_ledc ALTO = LED apagado.
 *   La inversión es:
 *     duty_ledc = DUTY_MAX - duty_calculado
 *
 *   Ejemplo con potenciómetro al 75% (duty_calculado = 6143):
 *     duty_ledc = 8191 - 6143 = 2048  → GPIO bajo el 25% del tiempo
 *     → corriente fluye el 75% del tiempo → brillo al 75%  ✓
 *
 * ── Lógica de estados acumulativos ───────────────────────────────────
 *
 *   Estado 0 (ROJO):
 *     - POT controla rojo en tiempo real (R variable, G=0, B=0)
 *     - Al pulsar: saved_red = duty_actual → avanza a estado 1
 *
 *   Estado 1 (AZUL):
 *     - Rojo fijo en saved_red, POT controla azul (R=saved_red, G=0, B variable)
 *     - Al pulsar: saved_blue = duty_actual → avanza a estado 2
 *
 *   Estado 2 (VERDE):
 *     - Rojo y azul fijos, POT controla verde (R=saved_red, G variable, B=saved_blue)
 *     - Al pulsar: saved_green = duty_actual → avanza a estado 3
 *
 *   Estado 3 (RGB combinado):
 *     - Se aplican saved_red, saved_green, saved_blue simultáneamente
 *     - POT no hace nada en este estado
 *     - Al pulsar: reset de los tres saved a 0 → vuelve a estado 0
 */

#include "button_led_module.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BTN_LED";

#define DUTY_MAX  ((1u << BTN_LED_DUTY_RES) - 1)   /* 8191 con 13 bits */

/* ─────────────────────────────────────────────────────────────────────
 * invert_duty — convierte duty de "cátodo común" a "ánodo común"
 *
 * El duty que calculamos internamente es intuitivo:
 *   0    = apagado
 *   8191 = brillo máximo
 *
 * Pero para ánodo común el hardware necesita el valor invertido.
 * Esta función hace esa conversión justo antes de escribir al LEDC.
 * ───────────────────────────────────────────────────────────────────── */
static inline uint32_t invert_duty(uint32_t duty)
{
    if (duty > DUTY_MAX) duty = DUTY_MAX;
    return DUTY_MAX - duty;
}

/* ─────────────────────────────────────────────────────────────────────
 * apply_duty_anodo — escribe R, G, B al hardware con inversión
 *
 * Recibe los duty en escala "intuitiva" (0=apagado, 8191=máximo)
 * y los invierte antes de escribir al LEDC para ánodo común.
 * Siempre escribe los TRES canales para evitar residuos del estado anterior.
 * ───────────────────────────────────────────────────────────────────── */
static void apply_duty_anodo(uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t lr = invert_duty(r);
    uint32_t lg = invert_duty(g);
    uint32_t lb = invert_duty(b);

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_R, lr));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_R));

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_G, lg));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_G));

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_B, lb));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, BTN_LED_CHANNEL_B));
}

/* ─────────────────────────────────────────────────────────────────────
 * duty_from_pot — convierte raw ADC (0–4095) a duty (0–8191)
 *
 * Aplica el umbral mínimo: si el valor es muy pequeño se trata como 0
 * para evitar que el LED quede con un brillo residual cuando el
 * potenciómetro está en su posición mínima.
 * ───────────────────────────────────────────────────────────────────── */
static uint32_t duty_from_pot(int pot_raw)
{
    uint32_t duty = ((uint32_t)pot_raw * DUTY_MAX) / 4095u;
    return (duty < BTN_DUTY_THRESHOLD) ? 0u : duty;
}

/* ─────────────────────────────────────────────────────────────────────
 * button_led_module_init
 * ───────────────────────────────────────────────────────────────────── */
void button_led_module_init(button_led_module_t *mod)
{
    mod->state            = BTN_LED_STATE_RED;
    mod->saved_red        = 0;
    mod->saved_blue       = 0;
    mod->saved_green      = 0;
    mod->duty_resolution  = BTN_LED_DUTY_RES;
    mod->btn_last_level   = 1;   /* pull-up: reposo en HIGH */
    mod->btn_last_tick_ms = 0;

    /* ── Botón: entrada con pull-up interno ──────────────────────────── */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BTN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    /* ── Timer LEDC para LED #2 (TIMER_1, separado del LED #1) ─────── */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BTN_LED_DUTY_RES,
        .timer_num       = BTN_LED_LEDC_TIMER,
        .freq_hz         = BTN_LED_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* ── Tres canales PWM ────────────────────────────────────────────── */
    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = BTN_LED_LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .hpoint     = 0,
        /* En ánodo común, duty inicial = DUTY_MAX → GPIO siempre alto → apagado */
        .duty       = DUTY_MAX,
    };

    ch.channel  = BTN_LED_CHANNEL_R;
    ch.gpio_num = BTN_LED_RED_GPIO;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    ch.channel  = BTN_LED_CHANNEL_G;
    ch.gpio_num = BTN_LED_GREEN_GPIO;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    ch.channel  = BTN_LED_CHANNEL_B;
    ch.gpio_num = BTN_LED_BLUE_GPIO;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));

    /* Confirmar apagado explícito con inversión correcta */
    apply_duty_anodo(0, 0, 0);

    ESP_LOGI(TAG, "LED#2 anodo comun: R=GPIO%d G=GPIO%d B=GPIO%d  Boton=GPIO%d",
             (int)BTN_LED_RED_GPIO, (int)BTN_LED_GREEN_GPIO,
             (int)BTN_LED_BLUE_GPIO, (int)BTN_GPIO);
    ESP_LOGI(TAG, "Estado 0: ajusta ROJO con el potenciometro");
}

/* ─────────────────────────────────────────────────────────────────────
 * button_led_module_process — llamar cada ~20 ms
 * ───────────────────────────────────────────────────────────────────── */
void button_led_module_process(button_led_module_t *mod, adc_module_t *adc)
{
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    /* ── 1. Leer botón con antirebote ───────────────────────────────── */
    int level = gpio_get_level(BTN_GPIO);

    if (mod->btn_last_level == 1 && level == 0) {
        /* Flanco descendente detectado */
        if ((now_ms - mod->btn_last_tick_ms) >= BTN_DEBOUNCE_MS) {
            mod->btn_last_tick_ms = now_ms;

            /* ── Guardar el valor actual y avanzar de estado ────────── */
            switch (mod->state) {

                case BTN_LED_STATE_RED:
                    /* Guardar el duty actual del rojo antes de pasar al azul.
                     * El duty actual lo leemos del potenciómetro en este momento. */
                    {
                        int pot_raw, pot_mv;
                        adc_module_read_pot(adc, &pot_raw, &pot_mv);
                        mod->saved_red = duty_from_pot(pot_raw);
                        ESP_LOGI(TAG, "Rojo guardado: duty=%lu (%.1f%%)  -> Estado: AZUL",
                                 mod->saved_red,
                                 (float)mod->saved_red * 100.0f / DUTY_MAX);
                    }
                    mod->state = BTN_LED_STATE_BLUE;
                    break;

                case BTN_LED_STATE_BLUE:
                    {
                        int pot_raw, pot_mv;
                        adc_module_read_pot(adc, &pot_raw, &pot_mv);
                        mod->saved_blue = duty_from_pot(pot_raw);
                        ESP_LOGI(TAG, "Azul guardado:  duty=%lu (%.1f%%)  -> Estado: VERDE",
                                 mod->saved_blue,
                                 (float)mod->saved_blue * 100.0f / DUTY_MAX);
                    }
                    mod->state = BTN_LED_STATE_GREEN;
                    break;

                case BTN_LED_STATE_GREEN:
                    {
                        int pot_raw, pot_mv;
                        adc_module_read_pot(adc, &pot_raw, &pot_mv);
                        mod->saved_green = duty_from_pot(pot_raw);
                        ESP_LOGI(TAG, "Verde guardado: duty=%lu (%.1f%%)  -> Estado: RGB",
                                 mod->saved_green,
                                 (float)mod->saved_green * 100.0f / DUTY_MAX);
                    }
                    mod->state = BTN_LED_STATE_RGB;
                    /* Aplicar inmediatamente la combinación guardada */
                    apply_duty_anodo(mod->saved_red,
                                     mod->saved_green,
                                     mod->saved_blue);
                    ESP_LOGI(TAG, "RGB combinado: R=%lu G=%lu B=%lu",
                             mod->saved_red, mod->saved_green, mod->saved_blue);
                    break;

                case BTN_LED_STATE_RGB:
                    /* Reset: volver al estado 0 con valores limpios */
                    mod->saved_red   = 0;
                    mod->saved_blue  = 0;
                    mod->saved_green = 0;
                    mod->state       = BTN_LED_STATE_RED;
                    apply_duty_anodo(0, 0, 0);   /* apagar antes de empezar de nuevo */
                    ESP_LOGI(TAG, "Reset -> Estado: ROJO  (ajusta con el potenciometro)");
                    break;

                default:
                    mod->state = BTN_LED_STATE_RED;
                    break;
            }
        }
    }
    mod->btn_last_level = level;

    /* ── 2. Actualizar LED según estado actual ───────────────────────── */
    int pot_raw, pot_mv;
    adc_module_read_pot(adc, &pot_raw, &pot_mv);
    uint32_t duty = duty_from_pot(pot_raw);

    switch (mod->state) {

        case BTN_LED_STATE_RED:
            /* Solo rojo variable, verde y azul apagados */
            apply_duty_anodo(duty, 0, 0);
            break;

        case BTN_LED_STATE_BLUE:
            /* Rojo fijo en saved_red, azul variable, verde apagado */
            apply_duty_anodo(0, 0, duty);
            break;

        case BTN_LED_STATE_GREEN:
            /* Rojo y azul fijos, verde variable */
            apply_duty_anodo(0, duty, 0);
            break;

        case BTN_LED_STATE_RGB:
            /* Combinación fija de los tres valores guardados.
             * El potenciómetro NO modifica nada en este estado.
             * Ya se aplicó al entrar en el estado — no hace falta re-aplicar
             * en cada tick, pero lo hacemos para garantizar consistencia. */
            apply_duty_anodo(mod->saved_red, mod->saved_green, mod->saved_blue);
            break;

        default:
            apply_duty_anodo(0, 0, 0);
            break;
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * button_led_state_name
 * ───────────────────────────────────────────────────────────────────── */
const char *button_led_state_name(btn_led_state_t state)
{
    switch (state) {
        case BTN_LED_STATE_RED:   return "AJUSTE ROJO";
        case BTN_LED_STATE_BLUE:  return "AJUSTE AZUL";
        case BTN_LED_STATE_GREEN: return "AJUSTE VERDE";
        case BTN_LED_STATE_RGB:   return "RGB COMBINADO";
        default:                  return "DESCONOCIDO";
    }
}