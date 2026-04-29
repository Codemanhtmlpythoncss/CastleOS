#include "shell_utils.h"

size_t str_length(const char* str) {
    size_t length = 0;

    while (str[length] != '\0') {
        length++;
    }

    return length;
}

void copy_string(char* destination, size_t destination_size, const char* source) {
    size_t i = 0;

    if (destination_size == 0) {
        return;
    }

    while (source[i] != '\0' && i + 1 < destination_size) {
        destination[i] = source[i];
        i++;
    }

    destination[i] = '\0';
}

void append_string(char* destination, size_t destination_size, const char* source) {
    size_t length = str_length(destination);
    size_t i = 0;

    if (length >= destination_size) {
        return;
    }

    while (source[i] != '\0' && length + 1 < destination_size) {
        destination[length++] = source[i++];
    }

    destination[length] = '\0';
}

void append_char(char* destination, size_t destination_size, char character) {
    size_t length = str_length(destination);

    if (length + 1 >= destination_size) {
        return;
    }

    destination[length] = character;
    destination[length + 1] = '\0';
}

void append_u64_string(char* destination, size_t destination_size, uint64_t value) {
    char digits[21];
    size_t index = 0;

    if (value == 0) {
        append_char(destination, destination_size, '0');
        return;
    }

    while (value > 0 && index < sizeof(digits)) {
        digits[index++] = (char) ('0' + (value % 10));
        value /= 10;
    }

    while (index > 0) {
        append_char(destination, destination_size, digits[--index]);
    }
}

void append_hex_u8_string(char* destination, size_t destination_size, uint8_t value) {
    char digits[] = "0123456789abcdef";

    append_string(destination, destination_size, "0x");
    append_char(destination, destination_size, digits[(value >> 4) & 0xF]);
    append_char(destination, destination_size, digits[value & 0xF]);
}

int parse_u16_decimal(const char* text, uint16_t* value) {
    uint32_t result = 0;
    size_t index = 0;

    if (!text || !value || text[0] == '\0') {
        return 0;
    }

    while (text[index] != '\0') {
        char character = text[index++];

        if (character < '0' || character > '9') {
            return 0;
        }

        result = result * 10 + (uint32_t) (character - '0');
        if (result > 65535) {
            return 0;
        }
    }

    *value = (uint16_t) result;
    return 1;
}

char* skip_spaces(char* text) {
    if (!text) {
        return 0;
    }

    while (*text == ' ' || *text == '\t') {
        text++;
    }

    if (*text == '\0') {
        return 0;
    }

    return text;
}

char* next_token(char** text) {
    char* token;

    if (!text || !*text) {
        return 0;
    }

    token = skip_spaces(*text);

    if (!token) {
        *text = 0;
        return 0;
    }

    *text = token;

    while (**text != '\0' && **text != ' ' && **text != '\t') {
        (*text)++;
    }

    if (**text != '\0') {
        **text = '\0';
        (*text)++;
    }

    return token;
}

size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
