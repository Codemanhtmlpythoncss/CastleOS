global long_mode_start
extern kernel_main
extern multiboot_info_ptr

section .text
bits 64
long_mode_start:
	mov ax, 0
	mov ss, ax
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	mov edi, dword [rel multiboot_info_ptr]

	call kernel_main
	hlt
