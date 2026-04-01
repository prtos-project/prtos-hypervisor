/*
 * FILE: boot.S
 *
 * AArch64 BAIL boot entry point
 * Sets up stack and jumps to C partition code
 *
 * SMP-aware: only vCPU0 zeroes BSS; each vCPU gets its own stack.
 * Mirrors the x86 boot.S logic for multi-vCPU partitions.
 *
 * http://www.prtos.org/
 */
#include <prtos.h>

#define MAIN_VCPU 0

.text
.align 4

.global start, _start

_start:
start:
    /* x0 = PCT physical address (passed by hypervisor in JMP_PARTITION) */
    /* Save PCT paddr — x0 will be clobbered by the HVC below */
    mov x19, x0

    /* Get vCPU ID via hypercall (no stack needed for HVC) */
    mov x0, #32            /* get_vcpuid_nr = hypercall #32 */
    hvc #0                 /* result in x0 = vCPU ID */
    mov x20, x0            /* x20 = vcpuid */

    /* Only the main vCPU (vCPU 0) zeroes BSS */
    cmp x20, #MAIN_VCPU
    bne 1f

    ldr x1, =_sbss
    ldr x2, =_ebss
    cmp x1, x2
    beq 1f
0:  str xzr, [x1], #8
    cmp x1, x2
    blt 0b
1:
    /* Set up per-vCPU stack: sp = _stack + (vcpuid + 1) * STACK_SIZE */
    ldr x2, =_stack
    ldr x3, =STACK_SIZE
    add x4, x20, #1        /* x4 = vcpuid + 1 */
    mul x4, x4, x3         /* x4 = (vcpuid + 1) * STACK_SIZE */
    add x2, x2, x4
    mov sp, x2

    /* Restore PCT paddr to x0 for init_libprtos */
    mov x0, x19

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

/*
 * _bail_trap_dispatch - Trap dispatch stub for virtual IRQ delivery.
 *
 * The hypervisor redirects the partition's PC here when a virtual IRQ
 * is pending. We save caller-saved regs, call C dispatch, restore regs,
 * then HVC to IRET which restores the original PC (and x0).
 */
.global _bail_trap_dispatch
_bail_trap_dispatch:
    /* Save caller-saved regs on stack (matching trap_ctxt_t layout) */
    sub sp, sp, #(22*8)
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    str x18, [sp, #144]
    stp x29, x30, [sp, #152]
    /* slot [168] = irq_nr, filled by C dispatch function */

    /* Call C dispatch: _bail_trap_dispatch_c(trap_ctxt_t *ctxt) */
    mov x0, sp
    bl  _bail_trap_dispatch_c

    /* Restore all caller-saved regs */
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldr x18, [sp, #144]
    ldp x29, x30, [sp, #152]
    add sp, sp, #(22*8)

    /*
     * HVC IRET: hypervisor restores original PC, SPSR, and x0.
     * x0 = PRTOS_IRET_NR (44), prtos_do_iret restores regs->x0 from saved value.
     */
    mov x0, #44
    hvc #0
    /* Should not reach here - hypervisor redirects PC via ERET */
    ret

.bss
.align 3
.globl _stack
_stack:
    .fill STACK_SIZE * CONFIG_MAX_NO_VCPUS, 1, 0
