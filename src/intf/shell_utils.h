#ifndef SHELL_UTILS_H
#define SHELL_UTILS_H

#include <stddef.h>
#include <stdint.h>

size_t str_length(const char* str);
void copy_string(char* destination, size_t destination_size, const char* source);
void append_string(char* destination, size_t destination_size, const char* source);
void append_char(char* destination, size_t destination_size, char character);
void append_u64_string(char* destination, size_t destination_size, uint64_t value);
void append_hex_u8_string(char* destination, size_t destination_size, uint8_t value);
int parse_u16_decimal(const char* text, uint16_t* value);
char* skip_spaces(char* text);
char* next_token(char** text);
size_t align_up(size_t value, size_t alignment);

#endif
