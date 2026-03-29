/*
 * FILE: boot.S
 *
 * RISC-V 64 BAIL boot entry point
 * Sets up stack and jumps to C partition code
 *
 * SMP-aware: only vCPU0 zeroes BSS; each vCPU gets its own stack.
 *
 * www.prtos.org
 */
#include <prtos.h>

#define MAIN_VCPU 0

.text
.align 4

.global start, _start

_start:
start:
    /* a0 = hartid (0), a1 = PCT physical address (SBI convention) */
    /* Save PCT paddr from a1 — a1 will be clobbered by the ecall below */
    mv   s0, a1

    /* Get vCPU ID via hypercall (no stack needed for ecall) */
    li   a0, 32            /* get_vcpuid_nr = hypercall #32 */
    ecall                  /* result in a0 = vCPU ID */
    mv   s1, a0            /* s1 = vcpuid */

    /* Only the main vCPU (vCPU 0) zeroes BSS */
    bnez s1, 1f

    la   t0, _sbss
    la   t1, _ebss
    bge  t0, t1, 1f
0:  sd   zero, 0(t0)
    addi t0, t0, 8
    blt  t0, t1, 0b
1:
    /* Set up per-vCPU stack: sp = _stack + (vcpuid + 1) * STACK_SIZE */
    la   t0, _stack
    li   t1, STACK_SIZE
    addi t2, s1, 1          /* t2 = vcpuid + 1 */
    mul  t2, t2, t1         /* t2 = (vcpuid + 1) * STACK_SIZE */
    add  t0, t0, t2
    mv   sp, t0

    /* Restore PCT paddr to a0 for init_libprtos */
    mv   a0, s0

    /* Call init_libprtos(a0 = part_ctrl_table) */
    call init_libprtos

    /* Call init_arch() */
    call init_arch

    /* Call setup_irqs() */
    call setup_irqs

    /* Call partition_main() */
    call partition_main

    /* If partition_main returns, halt */
    call part_halt

2:  wfi
    j    2b

/*
 * _bail_trap_dispatch - Trap dispatch stub for virtual IRQ delivery.
 *
 * The hypervisor redirects the partition's PC here when a virtual IRQ
 * is pending. We save caller-saved regs, call C dispatch, restore regs,
 * then ecall to IRET which restores the original PC (and a0).
 */
.global _bail_trap_dispatch
_bail_trap_dispatch:
    /* Save caller-saved regs on stack (matching trap_ctxt_t layout) */
    addi sp, sp, -(20*8)
    sd   a0, 0(sp)
    sd   a1, 8(sp)
    sd   a2, 16(sp)
    sd   a3, 24(sp)
    sd   a4, 32(sp)
    sd   a5, 40(sp)
    sd   a6, 48(sp)
    sd   a7, 56(sp)
    sd   t0, 64(sp)
    sd   t1, 72(sp)
    sd   t2, 80(sp)
    sd   t3, 88(sp)
    sd   t4, 96(sp)
    sd   t5, 104(sp)
    sd   t6, 112(sp)
    sd   ra, 120(sp)
    sd   s0, 128(sp)
    /* slot [136] = irq_nr, filled by C dispatch function */

    /* Call C dispatch: _bail_trap_dispatch_c(trap_ctxt_t *ctxt) */
    mv   a0, sp
    call _bail_trap_dispatch_c

    /* Restore all caller-saved regs */
    ld   a0, 0(sp)
    ld   a1, 8(sp)
    ld   a2, 16(sp)
    ld   a3, 24(sp)
    ld   a4, 32(sp)
    ld   a5, 40(sp)
    ld   a6, 48(sp)
    ld   a7, 56(sp)
    ld   t0, 64(sp)
    ld   t1, 72(sp)
    ld   t2, 80(sp)
    ld   t3, 88(sp)
    ld   t4, 96(sp)
    ld   t5, 104(sp)
    ld   t6, 112(sp)
    ld   ra, 120(sp)
    ld   s0, 128(sp)
    addi sp, sp, (20*8)

    /*
     * ecall IRET: hypervisor restores original PC and a0.
     * a0 = PRTOS_IRET_NR (44), prtos_do_iret restores regs->a0 from saved value.
     */
    li   a0, 44
    ecall
    /* Should not reach here - hypervisor redirects PC */
    ret

.bss
.align 3
.globl _stack
_stack:
    .fill STACK_SIZE * CONFIG_MAX_NO_VCPUS, 1, 0
