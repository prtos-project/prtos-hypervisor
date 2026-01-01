/*
 * FILE: boot.S
 *
 * Generic boot code, sets up a valid stack and jumps to the C code
 *
 * www.prtos.org
 */

#include "multiboot.h"

.section .multiboot    /* Create separate section for easier linking script positioning */
.align 4
_multiboot_header:
    /* magic */
    .long __MULTIBOOT_HEADER_MAGIC
    /* flags */
    .long __MULTIBOOT_HEADER_FLAGS
    /* checksum */
    .long -(__MULTIBOOT_HEADER_MAGIC+__MULTIBOOT_HEADER_FLAGS)

.text
.align 4

.global start, _start, _halt_system

_start:
start:
        xorl %eax, %eax
        movl $_sbss, %edi
        movl $_ebss, %ecx
        subl %edi, %ecx
        rep
        stosb

        mov $_estack, %esp
        call rsw_main

_halt_system:
        cli
        hlt
        jmp _halt_system

.bss
.align 1024
.globl _stack
_stack:
	.zero (CONFIG_STACK_SIZE*1024)
.globl _estack
_estack:

.previous
