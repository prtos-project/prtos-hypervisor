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

#ifdef CONFIG_amd64
.global _start, start
_start:
start:
        movq %rbx, part_ctrl_table_ptr(%rip)
        leaq end_stack(%rip), %rsp
        call main_pbl
        movq part_ctrl_table_ptr(%rip), %rbx
        jmp *%rax
1:
        jmp 1b
.bss
.global start_stack
start_stack:
        .zero STACK_SIZE
.global end_stack
end_stack:
#endif
