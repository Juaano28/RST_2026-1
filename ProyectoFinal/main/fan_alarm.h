#ifndef MAIN_FAN_ALARM_H_
#define MAIN_FAN_ALARM_H_

#define FAN_PWM_GPIO    13
#define ALARM_LED_GPIO  12

// Inicializa el hardware de ventilador y LED
void fan_alarm_init(void);

// Velocidad: 0 a 100
void fan_set_speed(uint8_t speed_percent);

// Estado de alarma: true (parpadea), false (apagado)
void fan_set_alarm_state(bool active);

#endif