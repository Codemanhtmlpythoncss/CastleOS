#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init();
uint64_t get_time_ms();
void timer_callback();

#endif