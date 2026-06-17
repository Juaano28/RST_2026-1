#ifndef SETTINGS_H
#define SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

// Declaración de las funciones de temperatura para que main.c las conozca
float settings_get_target_temperature(void);
void settings_set_target_temperature(float temp);
float settings_get_max_temperature(void);
void settings_set_max_temperature(float temp); // ◄── Agregar esta línea faltante

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_H