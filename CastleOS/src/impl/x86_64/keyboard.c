#include <stdint.h>
#include "keyboard.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static void keyboard_debounce() {
    volatile uint32_t i;
    for (i = 0; i < 100000; i++) {
        __asm__ volatile("");
    }
}

static char normal_table[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,
    0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0, '*',
    0, ' ',
};

static char shift_table[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,
    0, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0,
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, '*',
    0, ' ',
};

static int left_shift = 0;
static int right_shift = 0;
static int left_ctrl = 0;
static int right_ctrl = 0;
static int left_alt = 0;
static int right_alt = 0;
static int caps_lock = 0;
static int extended_prefix = 0;

static int shift_active() {
    return left_shift || right_shift;
}

static int translate_printable(uint8_t code) {
    char base = normal_table[code];
    char shifted = shift_table[code];

    if (!base) {
        return KEY_NONE;
    }

    if (base >= 'a' && base <= 'z') {
        if (caps_lock ^ shift_active()) {
            return shifted;
        }

        return base;
    }

    return shift_active() ? shifted : base;
}

static int translate_extended(uint8_t code) {
    if (code == 0x48) {
        return KEY_UP;
    }

    if (code == 0x50) {
        return KEY_DOWN;
    }

    if (code == 0x4B) {
        return KEY_LEFT;
    }

    if (code == 0x4D) {
        return KEY_RIGHT;
    }

    if (code == 0x47) {
        return KEY_HOME;
    }

    if (code == 0x4F) {
        return KEY_END;
    }

    if (code == 0x53) {
        return KEY_DELETE;
    }

    if (code == 0x1C) {
        return KEY_ENTER;
    }

    if (code == 0x35) {
        return '/';
    }

    return KEY_NONE;
}

int keyboard_getkey() {
    static uint8_t last_code = 0;
    static int last_was_press = 0;

    while (1) {
        uint8_t scancode;
        uint8_t code;
        int released;
        int translated;

        while ((inb(0x64) & 0x01) == 0) {
        }

        scancode = inb(0x60);

        if (scancode == 0xE0) {
            extended_prefix = 1;
            continue;
        }

        released = (scancode & 0x80) != 0;
        code = (uint8_t) (scancode & 0x7F);

        if (extended_prefix) {
            extended_prefix = 0;

            if (code == 0x1D) {
                right_ctrl = !released;
                continue;
            }

            if (code == 0x38) {
                right_alt = !released;
                continue;
            }

            if (released) {
                continue;
            }

            translated = translate_extended(code);
            if (translated != KEY_NONE) {
                last_code = code;
                last_was_press = 1;
                keyboard_debounce();
                return translated;
            }

            continue;
        }

        if (code == 0x2A) {
            left_shift = !released;
            continue;
        }

        if (code == 0x36) {
            right_shift = !released;
            continue;
        }

        if (code == 0x1D) {
            left_ctrl = !released;
            continue;
        }

        if (code == 0x38) {
            left_alt = !released;
            continue;
        }

        if (code == 0x3A) {
            if (!released) {
                caps_lock = !caps_lock;
            }
            continue;
        }

        if (released) {
            last_was_press = 0;
            continue;
        }

        if (code == last_code && last_was_press) {
            continue;
        }

        if (code == 0x01) {
            last_code = code;
            last_was_press = 1;
            keyboard_debounce();
            return KEY_ESCAPE;
        }

        if (code == 0x0E) {
            last_code = code;
            last_was_press = 1;
            keyboard_debounce();
            return KEY_BACKSPACE;
        }

        if (code == 0x0F) {
            last_code = code;
            last_was_press = 1;
            keyboard_debounce();
            return KEY_TAB;
        }

        if (code == 0x1C) {
            last_code = code;
            last_was_press = 1;
            keyboard_debounce();
            return KEY_ENTER;
        }

        translated = translate_printable(code);
        if (translated != KEY_NONE) {
            last_code = code;
            last_was_press = 1;
            keyboard_debounce();
            return translated;
        }
    }
}
