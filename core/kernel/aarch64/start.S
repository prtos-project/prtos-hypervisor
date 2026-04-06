/*
 * FILE: start.S
 *
 * AArch64 PRTOS hypervisor entry point
 * Runs in EL2 (Hypervisor mode)
 *
 * Boot sequence:
 *   1. Zero BSS (physical addresses, before MMU)
 *   2. Set up page tables (identity + virtual mapping)
 *   3. Enable MMU via TTBR0_EL2 + SCTLR_EL2
 *   4. Jump to virtual addresses
 *   5. Remove identity mapping
 *   6. Call setup_kernel() in virtual address space
 *
 * http://www.prtos.org/
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
     *   x0 = boot cpu id (MPIDR Aff0)
     * Running in EL2 (Hypervisor mode).
     * MMU is off. All addresses are physical.
     */

    /* Disable interrupts */
    msr daifset, #0xf

    /* Save boot CPU ID */
    adrp x10, _boot_cpuid
    add  x10, x10, :lo12:_boot_cpuid
    str  w0, [x10]

    /* BSP is always logical CPU 0 */
    mov  x19, #0            /* x19 = cpu_id = 0 */

    /* Store physical address of _secondary_start for PSCI CPU_ON */
    adrp x10, _secondary_start_addr
    add  x10, x10, :lo12:_secondary_start_addr
    adrp x11, _secondary_start
    add  x11, x11, :lo12:_secondary_start
    str  x11, [x10]

    /* Set up boot stack (in .boot.data, at physical address) */
    adrp x10, _boot_stacks
    add  x10, x10, :lo12:_boot_stacks
    add  sp, x10, #BOOT_STACK_SIZE  /* cpu 0: base + 8KB = top of stack[0] */

    /*
     * Zero BSS. Use physical address aliases from linker script.
     */
    adrp x10, _sbss_phys
    add  x10, x10, :lo12:_sbss_phys
    adrp x11, _ebss_phys
    add  x11, x11, :lo12:_ebss_phys
    cmp  x10, x11
    b.ge _bss_done
_bss_loop:
    str  xzr, [x10], #8
    cmp  x10, x11
    b.lt _bss_loop
_bss_done:

    /*
     * Set up page tables for EL2 MMU:
     *   - Identity map the 1GB block containing PRTOS_LOAD_ADDR
     *   - Virtual map at CONFIG_PRTOS_OFFSET -> same physical block
     *   - Identity map MMIO region (0x00000000-0x3FFFFFFF)
     */
    bl   setup_boot_page_table

    /*
     * Set VBAR_EL2 to a boot trap handler before enabling MMU
     */
    adrp x10, _boot_trap
    add  x10, x10, :lo12:_boot_trap
    msr  vbar_el2, x10
    isb

    /* Enable EL2 MMU */
    bl   enable_mmu

    /*
     * MMU is now on. We are still executing at a physical address
     * (identity-mapped). Jump to virtual address space.
     * Compute _virtual_start VA = PA(_virtual_start) + (OFFSET - LOAD)
     * where PA = VA + PHYSOFFSET, so final = VA(_virtual_start)
     * which is just its link address.  Use ldr =.
     */
    ldr  x10, =_virtual_start
    br   x10
    /* NOT REACHED from here — we continue at _virtual_start */

.section .text, "ax"
.align 2
.globl _virtual_start
_virtual_start:
    /* Now executing in virtual address space */

    /* Remove the identity mapping (only the virtual mapping remains) */
    bl   remove_identity_mapping

    /* Switch sp to virtual boot stack address */
    ldr  x10, =_boot_stacks_va
    add  sp, x10, #BOOT_STACK_SIZE

    /* Set up exception vector (now at virtual address) */
    ldr  x10, =_exception_vectors
    msr  vbar_el2, x10
    isb

    /* Jump to C setup code (virtual addresses) */
    mov  x0, x19               /* x0 = cpu_id = 0 */
    ldr  x1, =__idle_kthreads  /* x1 = idle kthread[0] (virtual address) */
    bl   setup_kernel
    /* Should not return */
    b    _halt

#ifdef CONFIG_SMP
/*
 * _secondary_start - entry point for secondary CPUs started via PSCI
 *
 * On entry:
 *   x0 = cpu_id (passed by PSCI as context_id)
 * Running in EL2 with MMU off.
 */
.section .boot.text, "ax"
.globl _secondary_start
.type _secondary_start, @function
.align 2
_secondary_start:
    mov  x19, x0               /* x19 = cpu_id */

    /* Disable interrupts */
    msr  daifset, #0xf

    /* Set up per-CPU boot stack (physical, in .boot.data) */
    adrp x10, _boot_stacks
    add  x10, x10, :lo12:_boot_stacks
    add  x11, x19, #1
    mov  x12, #BOOT_STACK_SIZE
    mul  x11, x11, x12
    add  sp, x10, x11

    /* Re-establish the single identity-map L1 entry that the BSP removed.
     * Do NOT call setup_boot_page_table here — it zeros all page tables,
     * which races with other secondary CPUs that may already have MMU on.
     * The rest of the page tables (L0, L1 MMIO, L1 kernel) are still valid
     * from the BSP's setup_boot_page_table call. */
    adrp x10, _boot_pt_l1_id
    add  x10, x10, :lo12:_boot_pt_l1_id
    ldr  x3, =CONFIG_PRTOS_LOAD_ADDR
    lsr  x4, x3, #30           /* L1 index = PA >> 30 */
    and  x4, x4, #0x1FF
    mov  x5, #(1 << 30)
    sub  x6, x5, #1
    bic  x5, x3, x6            /* PA base (1GB-aligned) */
    ldr  x6, =(0x1 | (0 << 2) | (1 << 10) | (3 << 8))  /* BLOCK_FLAGS */
    orr  x5, x5, x6            /* block descriptor */
    str  x5, [x10, x4, lsl #3] /* Restore identity block entry */
    dsb  sy
    isb

    /* Enable MMU (page tables are otherwise intact from BSP) */
    bl   enable_mmu

    /* Jump to virtual address space */
    ldr  x10, =_secondary_virtual
    br   x10

.section .text, "ax"
.align 2
.globl _secondary_virtual
_secondary_virtual:
    /* Now in virtual address space */

    /* Ensure SP_ELx is selected (not SP_EL0) so IRQ exceptions
     * use the Current EL SP_ELx vectors (offset 0x200+) */
    msr  spsel, #1
    isb

    /* Switch sp to virtual boot stack address */
    ldr  x10, =_boot_stacks_va
    add  x11, x19, #1
    mov  x12, #BOOT_STACK_SIZE
    mul  x11, x11, x12
    add  sp, x10, x11

    /* Set up exception vector (virtual) */
    ldr  x10, =_exception_vectors
    msr  vbar_el2, x10
    isb

    /* Set per-CPU ID in TPIDR_EL2 before calling C code */
    msr  tpidr_el2, x19

    /* Calculate idle kthread address: __idle_kthreads + cpu_id * IDLE_KTHREAD_SIZE */
    ldr  x1, =__idle_kthreads
    mov  x12, #IDLE_KTHREAD_SIZE
    mul  x12, x12, x19
    add  x1, x1, x12

    /* Call init_secondary_cpu(cpu_id, idle_kthread) */
    mov  x0, x19
    bl   init_secondary_cpu
    /* Should not return */
    b    _halt
#endif

.section .text, "ax"
_halt:
    wfe
    b    _halt

.align 2
.globl __halt_system
.type __halt_system, @function
__halt_system:
    wfe
    b    __halt_system

/*
 * kthread_startup_wrapper - wrapper for new kthread entry
 * x19 = start_up function pointer
 * x20 = entry_point argument
 */
.globl kthread_startup_wrapper
.type kthread_startup_wrapper, @function
.align 2
kthread_startup_wrapper:
    mov  x0, x20            /* entry_point as first arg */
    blr  x19               /* call start_up(entry_point) */
    /* If start_up returns, halt */
    b    _halt

/* Early boot trap handler — just spin */
.section .boot.text, "ax"
.align 11
_boot_trap:
    wfe
    b    _boot_trap

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
 */
.section .bss, "aw"
.align 8
.globl __idle_kthreads
__idle_kthreads:
    .space IDLE_KTHREAD_SIZE * CONFIG_NO_CPUS

.section .boot.data, "aw"
.align 4
.globl _boot_cpuid
_boot_cpuid:
    .word 0

.align 3
.globl _secondary_start_addr
_secondary_start_addr:
    .quad 0
