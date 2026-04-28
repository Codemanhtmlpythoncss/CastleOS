#include <stddef.h>

int strcmp(char* a, char* b) {
    size_t i = 0;

    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return a[i] - b[i];
        }
        i++;
    }

    return a[i] - b[i];
}