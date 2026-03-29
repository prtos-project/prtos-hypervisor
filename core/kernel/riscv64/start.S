/*
 * FILE: start.S
 *
 * RISC-V 64-bit PRTOS hypervisor entry point
 * Runs in HS-mode (Hypervisor Supervisor mode)
 *
 * Boot sequence:
 *   1. Zero BSS (physical addresses, before MMU)
 *   2. Set up Sv39 page tables (identity + virtual mapping)
 *   3. Enable MMU via satp
 *   4. Jump to virtual addresses
 *   5. Remove identity mapping
 *   6. Call setup_kernel() in virtual address space
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

/*
 * All code in .boot.text uses .option norelax to prevent the linker
 * from using GOT-based relocations. The GOT is placed in a virtual
 * section, which is too far from .boot.text (physical addresses).
 * We use lla (auipc+addi, PC-relative) for all address loads in boot code.
 */
.option norelax

.globl start
.type start, @function
.align 2
start:
    /*
     * Entry from RSW:
     *   a0 = boot hartid (physical hart that is BSP)
     * Running in HS-mode (Hypervisor Supervisor mode).
     * MMU is off (satp = 0, Bare mode). All addresses are physical.
     */

    /* Disable interrupts */
    csrw sie, zero
    csrci sstatus, 2    /* Clear SIE bit */

    /* Save boot hartid for SMP startup */
    lla t0, _boot_hartid
    sw a0, 0(t0)

    /* Save physical address of _secondary_start for SMP setup_smp() */
    lla t0, _secondary_start_addr
    lla t1, _secondary_start
    sd  t1, 0(t0)

    /* BSP is always logical CPU 0 */
    li s0, 0            /* s0 = cpu_id = 0 */

    /* Set tp = cpu_id for GET_CPU_ID() */
    mv tp, s0

    /* Set up boot stack (in .boot.data, at physical address) */
    lla sp, _boot_stacks
    li t0, BOOT_STACK_SIZE
    add sp, sp, t0      /* cpu 0: base + 8KB = top of stack[0] */

    /*
     * Zero BSS. Use physical address aliases from linker script
     * since _sbss/_ebss are virtual and unreachable via auipc from .boot.text.
     */
    lla  t0, _sbss_phys
    lla  t1, _ebss_phys
    bgeu t0, t1, _bss_done
_bss_loop:
    sd zero, 0(t0)
    addi t0, t0, 8
    bltu t0, t1, _bss_loop
_bss_done:

    /*
     * Set up early Sv39 page tables:
     *   - Identity map the 1GB gigapage containing PRTOS_LOAD_ADDR
     *   - Virtual map at CONFIG_PRTOS_OFFSET → same physical gigapage
     */
    call setup_boot_page_table

    /*
     * Set stvec to the physical address of a simple trap handler
     * (in .boot.text) before enabling MMU, in case of early faults.
     */
    lla t0, _boot_trap
    csrw stvec, t0

    /* Enable Sv39 MMU */
    call enable_mmu

    /*
     * MMU is now on. We are still executing at a physical address
     * (identity-mapped). Jump to virtual address space.
     *
     * We cannot use 'la _virtual_start' because the virtual symbol is
     * far from the current PC (>2GB). Instead, compute:
     *   VA = PA_of_this_code + (CONFIG_PRTOS_OFFSET - CONFIG_PRTOS_LOAD_ADDR)
     * But more precisely, we load the absolute VA via two instructions.
     */
    lla  t0, _virtual_start_phys   /* PA of _virtual_start (linker alias) */
    li   t1, CONFIG_PRTOS_OFFSET
    li   t2, CONFIG_PRTOS_LOAD_ADDR
    sub  t1, t1, t2               /* t1 = VA - PA offset */
    add  t0, t0, t1               /* t0 = virtual address */
    jr   t0
    /* NOT REACHED from here — we continue at _virtual_start */

.section .text, "ax"
.align 2
.globl _virtual_start
_virtual_start:
    /* Now executing in virtual address space */

    /* Remove the identity mapping (only the virtual mapping remains) */
    call remove_identity_mapping

    /* Switch sp to virtual boot stack address */
    la   sp, _boot_stacks_va
    li   t0, BOOT_STACK_SIZE
    add  sp, sp, t0

    /* Set up trap vector (now at virtual address) */
    la t0, _trap_entry
    csrw stvec, t0

    /* Jump to C setup code (virtual addresses) */
    mv a0, s0               /* a0 = cpu_id = 0 */
    la a1, __idle_kthreads   /* a1 = idle kthread[0] (virtual address) */
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
 * We enable the MMU using the same page table as BSP and jump to virtual.
 */
.section .boot.text, "ax"
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

    /* Set up per-CPU boot stack (physical, in .boot.data) */
    lla sp, _boot_stacks
    li t0, BOOT_STACK_SIZE
    addi t1, s0, 1
    mul t0, t0, t1
    add sp, sp, t0

    /* Restore identity mapping in _boot_pt_l1.
     * BSP's remove_identity_mapping cleared the identity-map PTE,
     * but we need it: enable_mmu sets satp with _boot_pt_l1 while
     * we are still executing at physical addresses. */
    call setup_boot_page_table

    /* Enable MMU with the same page table as BSP */
    call enable_mmu

    /* Jump to virtual address space */
    lla  t0, _secondary_virtual_phys
    li   t1, CONFIG_PRTOS_OFFSET
    li   t2, CONFIG_PRTOS_LOAD_ADDR
    sub  t1, t1, t2
    add  t0, t0, t1
    jr   t0

.section .text, "ax"
.align 2
.globl _secondary_virtual
_secondary_virtual:
    /* Now in virtual address space */

    /* Switch sp to virtual boot stack address */
    la   sp, _boot_stacks_va
    li   t0, BOOT_STACK_SIZE
    addi t1, s0, 1
    mul  t0, t0, t1
    add  sp, sp, t0

    /* Set up trap vector (virtual) */
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

.section .text, "ax"
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

/* Early boot trap handler — just spin */
.section .boot.text, "ax"
.align 2
_boot_trap:
    wfi
    j _boot_trap

/*
 * Per-CPU boot stacks in .boot.data (physical addresses, available before MMU)
 */
.section .boot.data, "aw"
.align 12
.globl _boot_stacks
_boot_stacks:
    .space BOOT_STACK_SIZE * CONFIG_NO_CPUS

/*
 * Per-CPU idle kthread structures (CONFIG_NO_CPUS * 16KB)
 * sizeof(kthread_t) = sizeof(kthread_ctrl) + CONFIG_KSTACK_SIZE.
 */
.section .bss, "aw"
.align 8
.globl __idle_kthreads
__idle_kthreads:
    .space IDLE_KTHREAD_SIZE * CONFIG_NO_CPUS

.section .boot.data, "aw"
.align 4
.globl _boot_hartid
_boot_hartid:
    .word 0

.align 8
.globl _secondary_start_addr
_secondary_start_addr:
    .quad 0
