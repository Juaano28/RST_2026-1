/**
 * Application entry point.
 */

#include "nvs_flash.h"
#include "driver/gpio.h"

#include "wifi_app.h"
#include "rgb_led.h"
#include "http_server.h"


static void configure_led(void)
{
    
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
	rgb_led_pwm_init();
}


void app_main(void)
{
    // Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	configure_led();
	// Start Wifi
	wifi_app_start();
}

