#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

#define MOUSE_BUTTON_LEFT 0x01
#define MOUSE_BUTTON_RIGHT 0x02
#define MOUSE_BUTTON_MIDDLE 0x04

struct MouseState {
    int x;
    int y;
    uint8_t buttons;
    int visible;
};

void mouse_init();
void mouse_enable_cursor();
void mouse_disable_cursor();
void mouse_get_state(struct MouseState* state);
void mouse_set_position(int x, int y);
void mouse_handle_interrupt();
int mouse_poll();

#endif
