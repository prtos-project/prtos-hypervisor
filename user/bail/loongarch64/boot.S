/*
 * FILE: boot.S
 *
 * LoongArch 64 BAIL boot entry point
 * Sets up stack and jumps to C partition code
 *
 * SMP-aware: only vCPU0 zeroes BSS; each vCPU gets its own stack.
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
    /* a0 = PCT physical address provided by hypervisor */
    move    $s0, $a0            /* Save PCT paddr */

    /* Get vCPU ID via hypercall */
    li.w    $a0, 32             /* get_vcpuid_nr = hypercall #32 */
    syscall 0                   /* result in a0 = vCPU ID */
    move    $s1, $a0            /* s1 = vcpuid */

    /* Only the main vCPU (vCPU 0) zeroes BSS */
    bnez    $s1, 1f

    la.local $t0, _sbss
    la.local $t1, _ebss
    bgeu    $t0, $t1, 1f
0:  st.d    $zero, $t0, 0
    addi.d  $t0, $t0, 8
    bltu    $t0, $t1, 0b
1:
    /* Set up per-vCPU stack: sp = _stack + (vcpuid + 1) * STACK_SIZE */
    la.local $t0, _stack
    li.w    $t1, STACK_SIZE
    addi.w  $t2, $s1, 1         /* t2 = vcpuid + 1 */
    mul.d   $t2, $t2, $t1       /* t2 = (vcpuid + 1) * STACK_SIZE */
    add.d   $t0, $t0, $t2
    move    $sp, $t0

    /* Restore PCT paddr to a0 for init_libprtos */
    move    $a0, $s0

    /* Call init_libprtos(a0 = part_ctrl_table) */
    bl      init_libprtos

    /* Call init_arch() */
    bl      init_arch

    /* Call setup_irqs() */
    bl      setup_irqs

    /* Call partition_main() */
    bl      partition_main

    /* If partition_main returns, halt */
    bl      part_halt

2:  idle    0
    b       2b

/*
 * _bail_trap_dispatch - Trap dispatch stub for virtual IRQ delivery.
 *
 * The hypervisor redirects the partition's PC here when a virtual IRQ
 * is pending. We save caller-saved regs, call C dispatch, restore regs,
 * then syscall IRET which restores the original PC (and a0).
 */
.global _bail_trap_dispatch
_bail_trap_dispatch:
    /* Save caller-saved regs on stack */
    addi.d  $sp, $sp, -(20*8)
    st.d    $a0, $sp,  0
    st.d    $a1, $sp,  8
    st.d    $a2, $sp, 16
    st.d    $a3, $sp, 24
    st.d    $a4, $sp, 32
    st.d    $a5, $sp, 40
    st.d    $a6, $sp, 48
    st.d    $a7, $sp, 56
    st.d    $t0, $sp, 64
    st.d    $t1, $sp, 72
    st.d    $t2, $sp, 80
    st.d    $t3, $sp, 88
    st.d    $t4, $sp, 96
    st.d    $t5, $sp, 104
    st.d    $t6, $sp, 112
    st.d    $ra, $sp, 120
    st.d    $fp, $sp, 128

    /* Call C dispatch: _bail_trap_dispatch_c(trap_ctxt_t *ctxt) */
    move    $a0, $sp
    bl      _bail_trap_dispatch_c

    /* Restore all caller-saved regs */
    ld.d    $a0, $sp,  0
    ld.d    $a1, $sp,  8
    ld.d    $a2, $sp, 16
    ld.d    $a3, $sp, 24
    ld.d    $a4, $sp, 32
    ld.d    $a5, $sp, 40
    ld.d    $a6, $sp, 48
    ld.d    $a7, $sp, 56
    ld.d    $t0, $sp, 64
    ld.d    $t1, $sp, 72
    ld.d    $t2, $sp, 80
    ld.d    $t3, $sp, 88
    ld.d    $t4, $sp, 96
    ld.d    $t5, $sp, 104
    ld.d    $t6, $sp, 112
    ld.d    $ra, $sp, 120
    ld.d    $fp, $sp, 128
    addi.d  $sp, $sp, (20*8)

    /* syscall IRET: hypervisor restores original PC and a0 */
    li.w    $a7, 0
    li.w    $a0, 44
    syscall 0
    /* Should not reach here */
    jirl    $zero, $ra, 0

.bss
.align 3
.globl _stack
_stack:
    .fill STACK_SIZE * CONFIG_MAX_NO_VCPUS, 1, 0
