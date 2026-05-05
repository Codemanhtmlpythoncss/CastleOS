#include <stdint.h>
#include "mouse.h"

#define MOUSE_X_DIVISOR 6
#define MOUSE_Y_DIVISOR 10
#define MOUSE_MAX_STEP 3

static struct MouseState mouse_state = {0, 0, 0, 0};
static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];
static int mouse_remainder_x = 0;
static int mouse_remainder_y = 0;

static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static int mouse_wait_input_clear() {
    uint32_t attempts = 1000000;
    while ((inb(0x64) & 0x02) != 0) {
        if (attempts-- == 0) return 0;
    }
    return 1;
}

static int mouse_wait_output_full() {
    uint32_t attempts = 1000000;
    while ((inb(0x64) & 0x01) == 0) {
        if (attempts-- == 0) return 0;
    }
    return 1;
}

void mouse_init() {
    uint8_t status;

    // Enable auxiliary device
    if (!mouse_wait_input_clear()) return;
    outb(0x64, 0xA8);

    // Enable interrupts
    if (!mouse_wait_input_clear()) return;
    outb(0x64, 0x20);
    if (!mouse_wait_output_full()) return;
    status = inb(0x60);
    status |= 0x02; // Enable mouse interrupt
    if (!mouse_wait_input_clear()) return;
    outb(0x64, 0x60);
    if (!mouse_wait_input_clear()) return;
    outb(0x60, status);

    // Set defaults
    if (!mouse_wait_input_clear()) return;
    outb(0x64, 0xD4);
    if (!mouse_wait_input_clear()) return;
    outb(0x60, 0xF6); // Set defaults
    if (!mouse_wait_output_full()) return;
    (void)inb(0x60);

    // Enable data reporting
    if (!mouse_wait_input_clear()) return;
    outb(0x64, 0xD4);
    if (!mouse_wait_input_clear()) return;
    outb(0x60, 0xF4); // Enable
    if (!mouse_wait_output_full()) return;
    (void)inb(0x60);
}

void mouse_enable_cursor() {
    mouse_state.visible = 1;
}

void mouse_disable_cursor() {
    mouse_state.visible = 0;
}

void mouse_get_state(struct MouseState* state) {
    state->x = mouse_state.x;
    state->y = mouse_state.y;
    state->buttons = mouse_state.buttons;
    state->visible = mouse_state.visible;
}

void mouse_set_position(int x, int y) {
    mouse_state.x = x;
    mouse_state.y = y;
    mouse_remainder_x = 0;
    mouse_remainder_y = 0;
}

static int clamp_step(int value) {
    if (value > MOUSE_MAX_STEP) {
        return MOUSE_MAX_STEP;
    }

    if (value < -MOUSE_MAX_STEP) {
        return -MOUSE_MAX_STEP;
    }

    return value;
}

void mouse_handle_interrupt() {
    uint8_t data = inb(0x60);
    mouse_packet[mouse_cycle++] = data;

    if (mouse_cycle == 3) {
        mouse_cycle = 0;

        if ((mouse_packet[0] & 0x08) == 0) return; // Invalid packet

        int dx = mouse_packet[1];
        int dy = mouse_packet[2];

        if (dx & 0x80) dx |= 0xFFFFFF00;
        if (dy & 0x80) dy |= 0xFFFFFF00;

        mouse_remainder_x += dx;
        mouse_remainder_y -= dy;

        dx = clamp_step(mouse_remainder_x / MOUSE_X_DIVISOR);
        dy = clamp_step(mouse_remainder_y / MOUSE_Y_DIVISOR);

        mouse_remainder_x -= dx * MOUSE_X_DIVISOR;
        mouse_remainder_y -= dy * MOUSE_Y_DIVISOR;

        mouse_state.x += dx;
        mouse_state.y += dy;

        // Clamp to screen
        if (mouse_state.x < 0) mouse_state.x = 0;
        if (mouse_state.y < 0) mouse_state.y = 0;
        if (mouse_state.x >= 80) mouse_state.x = 79; // Assuming 80x25
        if (mouse_state.y >= 25) mouse_state.y = 24;

        mouse_state.buttons = mouse_packet[0] & 0x07;
    }
}

int mouse_poll() {
    uint8_t status = inb(0x64);

    if ((status & 0x01) == 0 || (status & 0x20) == 0) {
        return 0;
    }

    mouse_handle_interrupt();
    return 1;
}
