#include "print.h"

const static size_t NUM_COLS = 80;
const static size_t NUM_ROWS = 25;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

struct Char {
    uint8_t character;
    uint8_t color;
};

struct Char* buffer = (struct Char*) 0xb8000;
size_t col = 0;
size_t row = 0;
uint8_t color = PRINT_COLOR_WHITE | PRINT_COLOR_BLACK << 4;

static void enable_cursor() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 14);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 15);
}

static void update_cursor() {
    uint16_t position = (uint16_t) (row * NUM_COLS + col);

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((position >> 8) & 0xFF));
}

void clear_row(size_t row) {
    struct Char empty = (struct Char) {
        .character = ' ',
        .color = color,
    };

    for (size_t col = 0; col < NUM_COLS; col++) {
        buffer[col + NUM_COLS * row] = empty;
    }
}

void print_clear() {
    for (size_t i = 0; i < NUM_ROWS; i++) {
        clear_row(i);
    }
    col = 0;
    row = 0;

    enable_cursor();
    update_cursor();
}

void print_newline() {
    col = 0;

    if (row < NUM_ROWS - 1) {
        row++;
        update_cursor();
        return;
    }

    for (size_t row = 1; row < NUM_ROWS; row++) {
        for (size_t col = 0; col < NUM_COLS; col++) {
            struct Char character = buffer[col + NUM_COLS * row];
            buffer[col + NUM_COLS * (row - 1)] = character;
        }
    }

    clear_row(NUM_ROWS - 1);
    update_cursor();
}

void print_char(char character) {
    if (character == '\n') {
        print_newline();
        return;
    }

    if (col >= NUM_COLS) {
        print_newline();
    }

    buffer[col + NUM_COLS * row] = (struct Char) {
        .character = (uint8_t) character,
        .color = color,
    };

    col++;

    if (col >= NUM_COLS) {
        print_newline();
        return;
    }

    update_cursor();
}

void print_str(char* str) {
    for (size_t i = 0; 1; i++) {
        char character = (uint8_t) str[i];

        if (character == '\0') {
            return;
        }

        print_char(character);
    }
}

void print_set_color(uint8_t foreground, uint8_t background) {
    color = foreground + (background << 4);
}

void print_backspace() {
    if (col == 0) {
        if (row == 0) {
            return;
        }

        row--;
        col = NUM_COLS - 1;
    } else {
        col--;
    }

    buffer[col + NUM_COLS * row] = (struct Char) {
        .character = ' ',
        .color = color,
    };

    update_cursor();
}

void print_set_cursor(size_t new_col, size_t new_row) {
    if (new_col >= NUM_COLS) {
        new_col = NUM_COLS - 1;
    }

    if (new_row >= NUM_ROWS) {
        new_row = NUM_ROWS - 1;
    }

    col = new_col;
    row = new_row;
    update_cursor();
}

size_t print_get_col() {
    return col;
}

size_t print_get_row() {
    return row;
}

void print_put_at(size_t target_col, size_t target_row, char character) {
    if (target_col >= NUM_COLS || target_row >= NUM_ROWS) {
        return;
    }

    buffer[target_col + NUM_COLS * target_row] = (struct Char) {
        .character = (uint8_t) character,
        .color = color,
    };
}

size_t print_get_num_cols() {
    return NUM_COLS;
}

size_t print_get_num_rows() {
    return NUM_ROWS;
}
