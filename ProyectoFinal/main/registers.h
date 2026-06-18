#ifndef REGISTERS_H
#define REGISTERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_REGISTERS 10

typedef struct {
    uint8_t hour;
    uint8_t minutes;
    bool days[7];             // 0=Lunes ... 6=Domingo
    uint8_t curtain_percent;  // Apertura objetivo de cortina 0..100 %
    bool active;
} timer_register_t;

void registers_init(void);
void registers_get_json(char *dest_buffer, size_t max_len);
bool registers_update(int reg_num, int hour, int minutes, uint8_t curtain_percent, const bool days[7]);
bool registers_erase(int reg_num);
void registers_copy_all(timer_register_t *dest, size_t max_items);

#endif
