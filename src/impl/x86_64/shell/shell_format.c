#include <stddef.h>
#include "shell_format.h"
#include "print.h"

void print_u64(uint64_t value) {
    char digits[21];
    size_t index = 0;

    if (value == 0) {
        print_char('0');
        return;
    }

    while (value > 0 && index < sizeof(digits)) {
        digits[index++] = (char) ('0' + (value % 10));
        value /= 10;
    }

    while (index > 0) {
        print_char(digits[--index]);
    }
}

void print_hex_u16(uint16_t value) {
    char digits[] = "0123456789abcdef";

    print_str("0x");

    for (int shift = 12; shift >= 0; shift -= 4) {
        print_char(digits[(value >> shift) & 0xF]);
    }
}

void print_hex_u8(uint8_t value) {
    char digits[] = "0123456789abcdef";

    print_char(digits[(value >> 4) & 0xF]);
    print_char(digits[value & 0xF]);
}
