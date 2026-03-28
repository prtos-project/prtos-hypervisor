/*
 * FILE: start.S
 *
 * RISC-V 64-bit PRTOS hypervisor entry point
 * Runs in HS-mode (Hypervisor Supervisor mode)
 *
 * www.prtos.org
 */

#include <linkage.h>
#include <prtosef.h>
#include <hypercalls.h>
#include <arch/asm_offsets.h>
#include <arch/irqs.h>
#include <arch/paging.h>
#include <arch/prtos_def.h>

#define BOOT_STACK_SIZE  8192
#define IDLE_KTHREAD_SIZE 16384

.section .boot.text, "ax"

.globl start
.type start, @function
.align 2
start:
    /*
     * Entry from RSW:
     *   a0 = boot hartid (physical hart that is BSP)
     * Running in HS-mode (Hypervisor Supervisor mode).
     */

    /* Disable interrupts */
    csrw sie, zero
    csrci sstatus, 2    /* Clear SIE bit */

    /* Save boot hartid for SMP startup */
    la t0, _boot_hartid
    sw a0, 0(t0)

    /* BSP is always logical CPU 0 */
    li s0, 0            /* s0 = cpu_id = 0 */

    /* Set tp = cpu_id for GET_CPU_ID() */
    mv tp, s0

    /* Set up stack pointer: _boot_stacks + (cpu_id + 1) * BOOT_STACK_SIZE */
    la sp, _boot_stacks
    li t0, BOOT_STACK_SIZE
    add sp, sp, t0      /* cpu 0: base + 8KB = top of stack[0] */

    /* Zero BSS */
    la t0, _sbss
    la t1, _ebss
    bgeu t0, t1, _bss_done
_bss_loop:
    sd zero, 0(t0)
    addi t0, t0, 8
    bltu t0, t1, _bss_loop
_bss_done:

    /* Set up trap vector for HS-mode */
    la t0, _trap_entry
    csrw stvec, t0

    /* Jump to C setup code */
    mv a0, s0               /* a0 = cpu_id = 0 */
    la a1, __idle_kthreads   /* a1 = idle kthread[0] */
    call setup_kernel
    /* Should not return */
    j _halt

#ifdef CONFIG_SMP
/*
 * _secondary_start - entry point for secondary harts started via SBI HSM
 *
 * SBI hart_start delivers:
 *   a0 = hartid (== cpu_id on QEMU virt)
 *   a1 = opaque (we pass cpu_id)
 *
 * Hart enters in S-mode with satp=0, sstatus.SIE=0.
 */
.globl _secondary_start
.type _secondary_start, @function
.align 2
_secondary_start:
    /* a0 = hartid, a1 = cpu_id (opaque from sbi_hart_start) */
    mv s0, a1               /* s0 = cpu_id */
    mv tp, s0               /* tp = cpu_id for GET_CPU_ID() */

    /* Disable interrupts */
    csrw sie, zero
    csrci sstatus, 2

    /* Set up per-CPU boot stack: _boot_stacks + (cpu_id + 1) * BOOT_STACK_SIZE */
    la sp, _boot_stacks
    li t0, BOOT_STACK_SIZE
    addi t1, s0, 1
    mul t0, t0, t1
    add sp, sp, t0

    /* Set up trap vector */
    la t0, _trap_entry
    csrw stvec, t0

    /* Calculate idle kthread address: __idle_kthreads + cpu_id * IDLE_KTHREAD_SIZE */
    la a1, __idle_kthreads
    li t0, IDLE_KTHREAD_SIZE
    mul t0, t0, s0
    add a1, a1, t0

    /* Call init_secondary_cpu(cpu_id, idle_kthread) */
    mv a0, s0
    call init_secondary_cpu
    /* Should not return */
    j _halt
#endif

_halt:
    wfi
    j _halt

.align 2
.globl __halt_system
.type __halt_system, @function
__halt_system:
    wfi
    j __halt_system

/*
 * kthread_startup_wrapper - wrapper for new kthread entry
 * s0 = start_up function pointer
 * s1 = entry_point argument
 */
.globl kthread_startup_wrapper
.type kthread_startup_wrapper, @function
.align 2
kthread_startup_wrapper:
    mv a0, s1           /* entry_point as first arg */
    jalr s0             /* call start_up(entry_point) */
    /* If start_up returns, halt */
    j _halt

/*
 * Per-CPU boot stacks (CONFIG_NO_CPUS * 8KB)
 */
.section .bss
.align 12
.globl _boot_stacks
_boot_stacks:
    .space BOOT_STACK_SIZE * CONFIG_NO_CPUS

/*
 * Per-CPU idle kthread structures (CONFIG_NO_CPUS * 16KB)
 * sizeof(kthread_t) = sizeof(kthread_ctrl) + CONFIG_KSTACK_SIZE.
 */
.align 8
.globl __idle_kthreads
__idle_kthreads:
    .space IDLE_KTHREAD_SIZE * CONFIG_NO_CPUS

.section .data
.align 4
.globl _boot_hartid
_boot_hartid:
    .word 0
