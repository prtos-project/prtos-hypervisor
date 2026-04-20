/*
 * FILE: start.S
 *
 * LoongArch 64-bit PRTOS hypervisor entry point
 * Runs at PLV0 (kernel privilege).
 *
 * Boot sequence:
 *   1. Configure DMW (Direct Mapped Windows) for cached/uncached access
 *   2. Zero BSS (physical addresses, before virtual transition)
 *   3. Jump to virtual addresses (via DMW)
 *   4. Set up exception handlers
 *   5. Call setup_kernel() in virtual address space
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

/* LoongArch CSR numbers */
#define CSR_CRMD     0x0
#define CSR_PRMD     0x1
#define CSR_EUEN     0x2
#define CSR_ECFG     0x4
#define CSR_ESTAT    0x5
#define CSR_ERA      0x6
#define CSR_BADV     0x7
#define CSR_EENTRY   0xC
#define CSR_CPUID    0x20
#define CSR_SAVE0    0x30
#define CSR_SAVE1    0x31
#define CSR_TID      0x40
#define CSR_TCFG     0x41
#define CSR_TICLR    0x44
#define CSR_DMW0     0x180
#define CSR_DMW1     0x181

/* DMW configuration values:
 * DMW0: VA[63:60]=0x8 → PA, uncached (MAT=0), PLV0+PLV3
 * DMW1: VA[63:60]=0x9 → PA, cached coherent (MAT=1), PLV0+PLV3
 * PLV3 enabled to allow hw-virt guests (running at PLV3) to use
 * DMW address ranges (0x8000.../0x9000...) for kernel virtual memory. */
#define DMW0_INIT  0x8000000000000009
#define DMW1_INIT  0x9000000000000019

/* CRMD bits */
#define CRMD_IE    (1 << 2)
#define CRMD_DA    (1 << 3)
#define CRMD_PG    (1 << 4)

.section .boot.text, "ax"

.globl start
.type start, @function
.align 4
start:
    /*
     * Entry from RSW bootloader (DA mode, PLV0).
     *
     * Boot sequence:
     *   1. Stay in DA mode for initial setup (BSS zero, etc.)
     *   2. Set up DMW windows including a temporary VSEG=0 window
     *   3. Switch DA→PG mode (DMW0 VSEG=0 covers current PC range)
     *   4. Jump to virtual address through DMW1 (VSEG=0x9)
     *   5. Reconfigure DMW0 for uncached I/O (VSEG=0x8)
     */

    /* Disable interrupts */
    li.w    $t0, CRMD_IE
    csrxchg $zero, $t0, CSR_CRMD

    /* Read CPU ID */
    csrrd   $t0, CSR_CPUID
    la.local $t1, _boot_cpuid
    st.w    $t0, $t1, 0

    /* BSP is always logical CPU 0 */
    li.w    $s0, 0              /* s0 = cpu_id = 0 */

    /* Save cpu_id to SAVE1 CSR for GET_CPU_ID() (csrwr swaps, use temp) */
    move    $t0, $s0
    csrwr   $t0, CSR_SAVE1

    /* Set up boot stack (in .boot.data, at physical address) */
    la.local $sp, _boot_stacks
    li.w    $t0, BOOT_STACK_SIZE
    add.d   $sp, $sp, $t0      /* cpu 0: base + 8KB = top of stack[0] */

    /*
     * Zero BSS. Use physical address aliases from linker script.
     */
    la.local $t0, _sbss_phys
    la.local $t1, _ebss_phys
    bgeu    $t0, $t1, _bss_done
_bss_loop:
    st.d    $zero, $t0, 0
    addi.d  $t0, $t0, 8
    bltu    $t0, $t1, _bss_loop
_bss_done:

    /* Save physical address of _secondary_start for SMP */
    la.local $t0, _secondary_start_addr
    la.local $t1, _secondary_start
    st.d    $t1, $t0, 0

    /*
     * Set up Direct Mapped Windows for the DA→PG transition.
     *
     * DMW0: VSEG=0x0, MAT=CC, PLV0 — temporary window that covers
     *        the boot physical address range (0x04xxxxxx) so that
     *        instructions at the current PC remain accessible after
     *        switching to PG mode.
     * DMW1: VSEG=0x9, MAT=CC, PLV0 — cached virtual address window
     *        (final configuration).
     */
#define DMW0_TRAMPOLINE  0x0000000000000011   /* VSEG=0, MAT=CC, PLV0 */
    li.d    $t0, DMW0_TRAMPOLINE
    csrwr   $t0, CSR_DMW0
    li.d    $t0, DMW1_INIT
    csrwr   $t0, CSR_DMW1

    /*
     * Prepare the virtual address for continuation BEFORE switching modes.
     * _virtual_start_phys is the LMA (physical) address of _virtual_start;
     * add the VA-PA offset to get the DMW1-mapped virtual address.
     */
    la.local $t0, _virtual_start_phys
    li.d    $t1, CONFIG_PRTOS_OFFSET
    li.d    $t2, CONFIG_PRTOS_LOAD_ADDR
    sub.d   $t1, $t1, $t2       /* t1 = VA - PA offset = 0x9000000000000000 */
    add.d   $s1, $t0, $t1       /* s1 = virtual address of _virtual_start (saved in callee-saved reg) */

    /*
     * Switch from DA mode to PG mode.
     * After this, address translation is ON.  The current PC (0x04xxxxxx)
     * is covered by the temporary DMW0 (VSEG=0), so execution continues.
     */
    li.w    $t0, CRMD_PG        /* PG=1, DA=0, PLV=0, IE=0 */
    csrwr   $t0, CSR_CRMD

    /* Jump to virtual address space through DMW1 (VSEG=0x9) */
    jr      $s1
    /* NOT REACHED from here — we continue at _virtual_start */

.section .text, "ax"
.align 4
.globl _virtual_start
_virtual_start:
    /*
     * Now executing in virtual address space (via DMW1, VSEG=0x9).
     *
     * Reconfigure DMW0 from the temporary VSEG=0 trampoline to
     * the final uncached I/O window: VSEG=0x8, MAT=SUC, PLV0.
     */
    li.d    $t0, DMW0_INIT
    csrwr   $t0, CSR_DMW0

    /* Switch sp to virtual boot stack address */
    la.abs  $sp, _boot_stacks_va
    li.w    $t0, BOOT_STACK_SIZE
    add.d   $sp, $sp, $t0

    /* Set up exception entry (virtual address) */
    la.abs  $t0, _trap_entry
    csrwr   $t0, CSR_EENTRY

    /* Set up TLB refill entry (must be physical address — DA mode during refill) */
    la.abs  $t0, _tlb_refill_entry
    li.d    $t1, CONFIG_PRTOS_OFFSET
    li.d    $t2, CONFIG_PRTOS_LOAD_ADDR
    sub.d   $t1, $t1, $t2       /* t1 = VA offset */
    sub.d   $t0, $t0, $t1       /* t0 = physical address */
    li.w    $t1, 0x88            /* CSR_TLBRENTRY */
    csrwr   $t0, 0x88           /* Set TLB refill handler */

    /* Enable interrupts in ECFG: TI (bit 11), IPI (bit 12), SWI0/1, HWI0-7 (bits 2-9) */
    li.w    $t0, (1 << 11) | (1 << 12) | (1 << 0) | (1 << 1) | (0xFF << 2)
    csrwr   $t0, CSR_ECFG

    /* Jump to C setup code (virtual addresses) */
    move    $a0, $s0             /* a0 = cpu_id = 0 */
    la.abs  $a1, __idle_kthreads /* a1 = idle kthread[0] (virtual address) */
    bl      setup_kernel
    /* Should not return */
    b       _halt

#ifdef CONFIG_SMP
/*
 * _secondary_start - entry point for secondary CPUs
 *
 * Secondary CPUs are started via IPI/mailbox mechanism.
 * The firmware delivers control here with cpu_id in some register.
 */
.section .boot.text, "ax"
.globl _secondary_start
.type _secondary_start, @function
.align 4
_secondary_start:
    /* Read cpu_id from CSR_CPUID (QEMU slave_boot_code does not pass it in $a0) */
    csrrd   $s0, 0x20            /* s0 = cpu_id from CSR_CPUID */
    move    $t0, $s0
    csrwr   $t0, CSR_SAVE1       /* Save cpu_id for GET_CPU_ID() (csrwr swaps) */

    /* Disable interrupts */
    li.w    $t0, CRMD_IE
    csrxchg $zero, $t0, CSR_CRMD

    /* Set up per-CPU boot stack (physical, in .boot.data) */
    la.local $sp, _boot_stacks
    li.w    $t0, BOOT_STACK_SIZE
    addi.d  $t1, $s0, 1
    mul.d   $t0, $t0, $t1
    add.d   $sp, $sp, $t0

    /*
     * Use same DMW trampoline technique as BSP:
     * DMW0 = VSEG=0 (temporary), DMW1 = VSEG=0x9 (cached)
     */
    li.d    $t0, DMW0_TRAMPOLINE
    csrwr   $t0, CSR_DMW0
    li.d    $t0, DMW1_INIT
    csrwr   $t0, CSR_DMW1

    /* Compute virtual address of _secondary_virtual */
    la.local $t0, _secondary_virtual_phys
    li.d    $t1, CONFIG_PRTOS_OFFSET
    li.d    $t2, CONFIG_PRTOS_LOAD_ADDR
    sub.d   $t1, $t1, $t2
    add.d   $s1, $t0, $t1       /* s1 = virtual address */

    /* Switch DA→PG */
    li.w    $t0, CRMD_PG
    csrwr   $t0, CSR_CRMD

    jr      $s1

.section .text, "ax"
.align 4
.globl _secondary_virtual
_secondary_virtual:
    /* Now in virtual address space — reconfigure DMW0 to final value */
    li.d    $t0, DMW0_INIT
    csrwr   $t0, CSR_DMW0

    /* Switch sp to virtual boot stack address */
    la.abs  $sp, _boot_stacks_va
    li.w    $t0, BOOT_STACK_SIZE
    addi.d  $t1, $s0, 1
    mul.d   $t0, $t0, $t1
    add.d   $sp, $sp, $t0

    /* Set up exception entry (virtual) */
    la.abs  $t0, _trap_entry
    csrwr   $t0, CSR_EENTRY

    /* Set up TLB refill entry (must be physical address — DA mode during refill) */
    la.abs  $t0, _tlb_refill_entry
    li.d    $t1, CONFIG_PRTOS_OFFSET
    li.d    $t2, CONFIG_PRTOS_LOAD_ADDR
    sub.d   $t1, $t1, $t2       /* t1 = VA - PA offset */
    sub.d   $t0, $t0, $t1       /* t0 = physical address */
    csrwr   $t0, 0x88           /* CSR_TLBRENTRY */

    /* Enable interrupts in ECFG: TI, IPI, SWI0/1, HWI0-7 */
    li.w    $t0, (1 << 11) | (1 << 12) | (1 << 0) | (1 << 1) | (0xFF << 2)
    csrwr   $t0, CSR_ECFG

    /* Calculate idle kthread address: __idle_kthreads + cpu_id * IDLE_KTHREAD_SIZE */
    la.abs  $a1, __idle_kthreads
    li.w    $t0, IDLE_KTHREAD_SIZE
    mul.d   $t0, $t0, $s0
    add.d   $a1, $a1, $t0

    /* Call init_secondary_cpu(cpu_id, idle_kthread) */
    move    $a0, $s0
    bl      init_secondary_cpu
    /* Should not return */
    b       _halt
#endif

.section .text, "ax"

/*
 * _enter_idle_on_own_stack - switch sp to idle kthread's own kstack area
 *                           then enter schedule + idle_task loop.
 *
 * a0 = new stack top (virtual address, 16-byte aligned)
 *
 * This avoids using the boot stack for the idle kthread during normal
 * operation. The boot stack may be accessible to guest partitions via
 * identity-mapped TLB entries (PLV3), so using a separate stack in .bss
 * isolates the idle kthread's saved context from potential corruption.
 */
.globl _enter_idle_on_own_stack
.type _enter_idle_on_own_stack, @function
.align 4
_enter_idle_on_own_stack:
    move    $sp, $a0
    bl      schedule
    bl      idle_task
    b       _halt

_halt:
    idle    0
    b       _halt

/*
 * context_switch_asm - low-level context switch between kthreads
 *
 * a0 = next kthread pointer (kthread_t *)
 * a1 = pointer to current_kthread pointer (kthread_t **)
 *
 * Saves callee-saved registers (fp, s0-s8, ra) on old stack,
 * stores sp to old kthread's ctrl.kstack,
 * loads sp from new kthread's ctrl.kstack,
 * restores callee-saved registers from new stack,
 * returns to the new kthread's saved return address.
 *
 * Frame layout (96 bytes) must match setup_kstack() in kthread.c:
 *   [sp+ 0] fp
 *   [sp+ 8] s0
 *   [sp+16] s1
 *   [sp+24] s2
 *   [sp+32] s3
 *   [sp+40] s4
 *   [sp+48] s5
 *   [sp+56] s6
 *   [sp+64] s7
 *   [sp+72] s8
 *   [sp+80] ra
 *
 * kstack_off = 8 (offsetof(kthread_t, ctrl) + offsetof(__kthread, kstack))
 */
#define KSTACK_OFF 8

.globl context_switch_asm
.type context_switch_asm, @function
.align 4
context_switch_asm:
    /* Save callee-saved registers on current stack */
    addi.d  $sp, $sp, -96
    st.d    $fp,  $sp, 0
    st.d    $s0,  $sp, 8
    st.d    $s1,  $sp, 16
    st.d    $s2,  $sp, 24
    st.d    $s3,  $sp, 32
    st.d    $s4,  $sp, 40
    st.d    $s5,  $sp, 48
    st.d    $s6,  $sp, 56
    st.d    $s7,  $sp, 64
    st.d    $s8,  $sp, 72
    st.d    $ra,  $sp, 80
    dbar    0

    /* Save old kthread sp: old->ctrl.kstack = sp */
    ld.d    $t0, $a1, 0             /* t0 = *current_ptr (old kthread) */
    st.d    $sp, $t0, KSTACK_OFF    /* old->ctrl.kstack = sp */

    /* Switch to new kthread */
    st.d    $a0, $a1, 0             /* *current_ptr = next */
    ld.d    $sp, $a0, KSTACK_OFF    /* sp = next->ctrl.kstack */
    dbar    0

    /* Restore new kthread's callee-saved registers */
    ld.d    $fp,  $sp, 0
    ld.d    $s0,  $sp, 8
    ld.d    $s1,  $sp, 16
    ld.d    $s2,  $sp, 24
    ld.d    $s3,  $sp, 32
    ld.d    $s4,  $sp, 40
    ld.d    $s5,  $sp, 48
    ld.d    $s6,  $sp, 56
    ld.d    $s7,  $sp, 64
    ld.d    $s8,  $sp, 72
    ld.d    $ra,  $sp, 80
    addi.d  $sp, $sp, 96

    ret     /* jr $ra - returns to new kthread's saved return address */

.align 4
.globl __halt_system
.type __halt_system, @function
__halt_system:
    idle    0
    b       __halt_system

/*
 * kthread_startup_wrapper - wrapper for new kthread entry
 * s0 = start_up function pointer
 * s1 (= $r24) = entry_point argument
 */
.globl kthread_startup_wrapper
.type kthread_startup_wrapper, @function
.align 4
kthread_startup_wrapper:
    move    $a0, $s1            /* entry_point as first arg */
    jirl    $ra, $s0, 0         /* call start_up(entry_point) */
    /* If start_up returns, halt */
    b       _halt

/*
 * Per-CPU boot stacks in .boot.data (physical addresses, available before DMW)
 */
.section .boot.data, "aw"
.align 12
.globl _boot_stacks
_boot_stacks:
    .space BOOT_STACK_SIZE * CONFIG_NO_CPUS

/*
 * Per-CPU idle kthread structures
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

.align 8
.globl _secondary_start_addr
_secondary_start_addr:
    .dword 0
