#ifndef KEYBOARD_H
#define KEYBOARD_H

enum {
    KEY_NONE = 0,
    KEY_ESCAPE = 0x100,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_ENTER,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
};

int keyboard_getkey();
void keyboard_init();

#endif
