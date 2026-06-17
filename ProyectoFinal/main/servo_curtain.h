#ifndef MAIN_SERVO_CURTAIN_H_
#define MAIN_SERVO_CURTAIN_H_

#include <stdint.h>
#include <stdbool.h>

#define SERVO_PWM_GPIO   7

// Inicializa el periférico PWM (LEDC) a 50Hz para el Servo
void servo_curtain_init(void);

// Establece la apertura de la cortina (0 a 100 %)
void servo_set_position(uint8_t open_percent);

// Funciones para anulación manual (Modo Manual vs Modo Automático)
void servo_set_manual_override(bool override_active);
bool servo_get_manual_override(void);

#endif /* MAIN_SERVO_CURTAIN_H_ */