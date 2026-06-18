#ifndef MAIN_RGB_LED_H_
#define MAIN_RGB_LED_H_

#include <stdint.h>

#define RGB_LED_RED_GPIO        21
#define RGB_LED_GREEN_GPIO      22
#define RGB_LED_BLUE_GPIO       23

void rgb_led_init(void);
void rgb_led_set_color(uint8_t red, uint8_t green, uint8_t blue);
void rgb_led_wifi_app_started(void);
void rgb_led_http_server_started(void);
void rgb_led_wifi_connected(void);

#endif
