#include "print.h"

#define NUM_COLS 80
#define NUM_ROWS 25
#define HISTORY_ROWS 512

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

struct Char {
    uint8_t character;
    uint8_t color;
};

static struct Char history_buffer[HISTORY_ROWS * NUM_COLS];
static struct Char* vga_buffer = (struct Char*) 0xb8000;
size_t col = 0;
size_t row = 0;
uint8_t color = PRINT_COLOR_WHITE | PRINT_COLOR_BLACK << 4;
static size_t cursor_col_abs = 0;
static size_t cursor_row_abs = 0;
static size_t viewport_row = 0;
static int cursor_visible = 1;

static struct Char blank_cell() {
    return (struct Char) {
        .character = ' ',
        .color = color,
    };
}

static void apply_cursor_visibility() {
    outb(0x3D4, 0x0A);
    outb(0x3D5, cursor_visible ? 14 : 0x20);
    outb(0x3D4, 0x0B);
    outb(0x3D5, cursor_visible ? 15 : 0);
}

static size_t max_viewport_row() {
    if (cursor_row_abs < NUM_ROWS - 1) {
        return 0;
    }

    return cursor_row_abs - (NUM_ROWS - 1);
}

static void sync_cursor_state() {
    if (cursor_row_abs < viewport_row) {
        row = 0;
    } else if (cursor_row_abs >= viewport_row + NUM_ROWS) {
        row = NUM_ROWS - 1;
    } else {
        row = cursor_row_abs - viewport_row;
    }

    if (cursor_col_abs >= NUM_COLS) {
        col = NUM_COLS - 1;
    } else {
        col = cursor_col_abs;
    }
}

static void update_cursor() {
    uint16_t position;

    sync_cursor_state();

    if (!cursor_visible || cursor_row_abs < viewport_row || cursor_row_abs >= viewport_row + NUM_ROWS) {
        apply_cursor_visibility();
        return;
    }

    apply_cursor_visibility();
    position = (uint16_t) (row * NUM_COLS + col);

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((position >> 8) & 0xFF));
}

static void clear_history_row(size_t target_row) {
    struct Char empty = blank_cell();

    if (target_row >= HISTORY_ROWS) {
        return;
    }

    for (size_t target_col = 0; target_col < NUM_COLS; target_col++) {
        history_buffer[target_col + NUM_COLS * target_row] = empty;
    }
}

static void render_viewport() {
    for (size_t visible_row = 0; visible_row < NUM_ROWS; visible_row++) {
        size_t source_row = viewport_row + visible_row;

        for (size_t visible_col = 0; visible_col < NUM_COLS; visible_col++) {
            struct Char cell = blank_cell();

            if (source_row < HISTORY_ROWS) {
                cell = history_buffer[visible_col + NUM_COLS * source_row];
            }

            vga_buffer[visible_col + NUM_COLS * visible_row] = cell;
        }
    }

    update_cursor();
}

static void drop_top_rows(size_t rows_to_drop) {
    if (rows_to_drop == 0) {
        return;
    }

    if (rows_to_drop >= HISTORY_ROWS) {
        rows_to_drop = HISTORY_ROWS - 1;
    }

    for (size_t target_row = 0; target_row + rows_to_drop < HISTORY_ROWS; target_row++) {
        for (size_t target_col = 0; target_col < NUM_COLS; target_col++) {
            history_buffer[target_col + NUM_COLS * target_row]
                = history_buffer[target_col + NUM_COLS * (target_row + rows_to_drop)];
        }
    }

    for (size_t target_row = HISTORY_ROWS - rows_to_drop; target_row < HISTORY_ROWS; target_row++) {
        clear_history_row(target_row);
    }

    if (cursor_row_abs >= rows_to_drop) {
        cursor_row_abs -= rows_to_drop;
    } else {
        cursor_row_abs = 0;
    }

    if (viewport_row >= rows_to_drop) {
        viewport_row -= rows_to_drop;
    } else {
        viewport_row = 0;
    }
}

static size_t ensure_row_visible(size_t target_row) {
    if (target_row >= HISTORY_ROWS) {
        size_t overflow = target_row - (HISTORY_ROWS - 1);
        drop_top_rows(overflow);
        target_row = HISTORY_ROWS - 1;
    }

    return target_row;
}

void clear_row(size_t target_row) {
    clear_history_row(viewport_row + target_row);
    render_viewport();
}

void print_clear() {
    for (size_t target_row = 0; target_row < HISTORY_ROWS; target_row++) {
        clear_history_row(target_row);
    }

    cursor_col_abs = 0;
    cursor_row_abs = 0;
    viewport_row = 0;
    col = 0;
    row = 0;
    cursor_visible = 1;
    render_viewport();
}

void print_follow_output() {
    viewport_row = max_viewport_row();
    render_viewport();
}

int print_is_scrolled() {
    return viewport_row != max_viewport_row();
}

void print_scroll_view(int lines) {
    size_t max_row = max_viewport_row();

    if (lines < 0) {
        size_t amount = (size_t) (-lines);

        if (amount > viewport_row) {
            viewport_row = 0;
        } else {
            viewport_row -= amount;
        }
    } else if (lines > 0) {
        size_t amount = (size_t) lines;

        if (viewport_row + amount > max_row) {
            viewport_row = max_row;
        } else {
            viewport_row += amount;
        }
    }

    render_viewport();
}

void print_newline() {
    cursor_col_abs = 0;
    cursor_row_abs = ensure_row_visible(cursor_row_abs + 1);
    print_follow_output();
}

void print_char(char character) {
    if (character == '\n') {
        print_newline();
        return;
    }

    if (cursor_col_abs >= NUM_COLS) {
        print_newline();
    }

    history_buffer[cursor_col_abs + NUM_COLS * cursor_row_abs] = (struct Char) {
        .character = (uint8_t) character,
        .color = color,
    };

    cursor_col_abs++;

    if (cursor_col_abs >= NUM_COLS) {
        print_newline();
        return;
    }

    print_follow_output();
}

void print_str(char* str) {
    for (size_t index = 0; 1; index++) {
        char character = (uint8_t) str[index];

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
    if (cursor_col_abs == 0) {
        if (cursor_row_abs == 0) {
            return;
        }

        cursor_row_abs--;
        cursor_col_abs = NUM_COLS - 1;
    } else {
        cursor_col_abs--;
    }

    history_buffer[cursor_col_abs + NUM_COLS * cursor_row_abs] = blank_cell();
    print_follow_output();
}

void print_set_cursor(size_t new_col, size_t new_row) {
    print_set_cursor_abs(new_col, viewport_row + new_row);
}

void print_set_cursor_abs(size_t new_col, size_t new_row) {
    int follow_latest = !print_is_scrolled();

    if (new_col >= NUM_COLS) {
        new_col = NUM_COLS - 1;
    }

    cursor_col_abs = new_col;
    cursor_row_abs = ensure_row_visible(new_row);

    if (follow_latest) {
        viewport_row = max_viewport_row();
    }

    render_viewport();
}

void print_set_cursor_visible(int visible) {
    cursor_visible = visible ? 1 : 0;
    update_cursor();
}

size_t print_get_col() {
    sync_cursor_state();
    return col;
}

size_t print_get_row() {
    sync_cursor_state();
    return row;
}

size_t print_get_abs_row() {
    return cursor_row_abs;
}

void print_put_at(size_t target_col, size_t target_row, char character) {
    print_put_abs_at(target_col, viewport_row + target_row, character);
}

void print_put_abs_at(size_t target_col, size_t target_row, char character) {
    if (target_col >= NUM_COLS) {
        return;
    }

    target_row = ensure_row_visible(target_row);

    history_buffer[target_col + NUM_COLS * target_row] = (struct Char) {
        .character = (uint8_t) character,
        .color = color,
    };
}

void print_refresh() {
    render_viewport();
}

size_t print_get_num_cols() {
    return NUM_COLS;
}

size_t print_get_num_rows() {
    return NUM_ROWS;
}
