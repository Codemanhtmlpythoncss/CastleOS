#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

void shell_init(uint32_t multiboot_info_addr);
void shell_run();

#endif
