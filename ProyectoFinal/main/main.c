#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"

#include "control_system.h"
#include "wifi_app.h"

static const char *TAG = "MAIN";

static void init_nvs_or_recover(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS incompatible o sin páginas; se borra y reinicializa");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    init_nvs_or_recover();

    /* Capa local STR primero: ADC/NTC, PWM fan/RGB/servo, NVS y agenda.
     * Así la regulación térmica no depende del servidor web ni del Wi-Fi. */
    init_obtain_time();
    control_system_init();
    control_system_start();

    /* Capa de red/presentación al final: AP+STA, SNTP, HTTP, OTA. */
    wifi_app_start();
}
