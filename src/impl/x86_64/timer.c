#include "timer.h"

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

volatile uint64_t tick = 0;

void timer_callback() {
    tick++;
}

uint64_t get_time_ms() {
    return tick;
}

void timer_wait(uint32_t milliseconds) {
    uint64_t end_time = get_time_ms() + milliseconds;
    while (get_time_ms() < end_time) {
        __asm__ volatile ("hlt");
    }
}

void timer_init() {
    uint32_t divisor = 1193180 / 1000;

    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}