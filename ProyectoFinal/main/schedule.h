#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdbool.h>
#include <stddef.h> 

#define MAX_REGISTERS 10

typedef struct {
    int hour;
    int minutes;
    bool days[7]; // Lunes a Domingo (0=Mon, 6=Sun)
    bool active;
} timer_register_t;

#ifdef __cplusplus
extern "C" {
#endif

void registers_init(void);
void registers_get_json(char *dest_buffer, size_t max_len); 
bool registers_update(int reg_num, int hour, int minutes, const char* days_str[7]);
bool registers_erase(int reg_num);

// NUEVO: Prototipo expuesto para el lazo de control horario
void check_schedules(void);

#ifdef __cplusplus
}
#endif

#endif // SCHEDULE_H