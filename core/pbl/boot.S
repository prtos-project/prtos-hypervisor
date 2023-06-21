/*
 * FILE: boot.S
 *
 * Generic boot code, sets up a valid stack and jumps to the C code
 *
 * www.prtos.org
 */

#include <config.h>
#define MIN_STACK_FRAME 0x60
#define STACK_SIZE 8192


#ifdef CONFIG_x86
.global _start, start
_start:
start:
        movl %ebx, part_ctrl_table_ptr
        movl $end_stack, %esp
        call main_pbl
        movl part_ctrl_table_ptr, %ebx
        jmp *%eax
1:
        jmp 1b
.bss
.global start_stack
start_stack:
        .zero STACK_SIZE
.global end_stack
end_stack:
#endif
