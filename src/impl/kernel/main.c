#include "print.h"
#include "keyboard.h"
#include "shell.h"
#include "timer.h"

void kernel_main(uint32_t multiboot_info_addr) {
	print_clear();
	print_set_color(PRINT_COLOR_GREEN, PRINT_COLOR_BLACK);
	print_str("Welcome to the 64-Bit CastleOS kernel made by Barnaby Raynes!");
	print_newline();
	print_str("                      |>>>                |>>>                |>>>\n______________________|___________________|___________________|______________________\n| [] [] [] [] [] []   | [] [] [] [] [] [] | [] [] [] [] [] [] |\n|_|_|_|_|_|_|_|_|_|_|__|_|_|_|_|_|_|_|_|_|__|_|_|_|_|_|_|_|_|_|_|\n|         ____        |      |____|      |        ____        |\n|________|____|_______|______|____|______|_______|____|________|");
	print_newline();
	print_str("Shell Loading...");
	timer_init();
	keyboard_init();
	shell_init(multiboot_info_addr);
	shell_run();
}
