/*
 * FILE: boot.S
 *
 * AArch64 BAIL boot entry point
 * Sets up stack and jumps to C partition code
 *
 * www.prtos.org
 */
#include <prtos.h>

.text
.align 4

.global start, _start

_start:
start:
    /* x0 = PCT physical address (passed by hypervisor in JMP_PARTITION) */

    /* Zero BSS */
    ldr x1, =_sbss
    ldr x2, =_ebss
    cmp x1, x2
    beq 1f
0:  str xzr, [x1], #8
    cmp x1, x2
    blt 0b
1:
    /* Set up stack: top of _stack area (STACK_SIZE bytes) */
    ldr x2, =_stack
    ldr x3, =STACK_SIZE
    add x2, x2, x3
    mov sp, x2

    /* Call init_libprtos(x0 = part_ctrl_table) */
    bl  init_libprtos

    /* Call init_arch() */
    bl  init_arch

    /* Call setup_irqs() */
    bl  setup_irqs

    /* Call partition_main() */
    bl  partition_main

    /* If partition_main returns, halt */
    bl  part_halt

2:  wfe
    b   2b

.bss
.align 3
.globl _stack
_stack:
    .fill STACK_SIZE, 1, 0
