/*
 * FILE: boot.S
 *
 * LoongArch 64 RSW boot entry point
 * Sets up a valid stack and jumps to the C code
 *
 * http://www.prtos.org/
 */

.section .text.boot, "ax"
.global  _start
.type    _start, @function
.align 4

_start:
    /* Disable interrupts */
    li.w    $t0, (1 << 2)
    csrxchg $zero, $t0, 0x0     /* Clear CRMD.IE */

    /*
     * Only CPU 0 boots the system. Secondary CPUs spin in a loop
     * waiting for the hypervisor to wake them via IPI.
     */
    csrrd   $t0, 0x20           /* CSR_CPUID */
    bnez    $t0, _secondary_spin

    /*
     * Stay in DA mode (Direct Address): VA = PA.
     * QEMU starts LoongArch64 in DA mode. The RSW bootloader
     * runs entirely at physical addresses, so no DMW or PG needed.
     * The PRTOS core will set up DMW and switch to PG mode itself.
     */

    /* Set stack (physical addresses in DA mode) */
    la.local $sp, _estack

    /* Save boot CPU ID */
    csrrd   $t0, 0x20           /* CSR_CPUID */
    la.local $t1, rsw_boot_cpuid
    st.w    $t0, $t1, 0

    b       rsw_main

/* Secondary CPUs: poll a per-CPU mailbox for the entry address.
 * Mailbox layout: PA 0x100 + cpuid*8 contains entry address.
 * The PRTOS core writes _secondary_start PA only for CPUs it wants.
 */
#define SECONDARY_MAILBOX_BASE  0x100

_secondary_spin:
    csrrd   $t1, 0x20                /* CSR_CPUID */
    slli.d  $t1, $t1, 3             /* cpuid * 8 */
    li.d    $t0, SECONDARY_MAILBOX_BASE
    add.d   $t2, $t0, $t1           /* t2 = &mailbox[cpuid] */
    ld.d    $t0, $t2, 0
    bnez    $t0, _secondary_go
    nop
    nop
    nop
    nop
    b       _secondary_spin

_secondary_go:
    /* Got entry address - jump with cpu_id in a0 */
    csrrd   $a0, 0x20                /* CSR_CPUID */
    jr      $t0                      /* Jump to _secondary_start */

.global  _halt_system
.type    _halt_system, @function
.align 4
_halt_system:
1:
    idle    0
    b       1b

.align 4
.bss
.align 4
.globl _stack
_stack:
	.zero (CONFIG_STACK_SIZE*1024)
.globl _estack
_estack:

.data
.align 4
.globl rsw_boot_cpuid
rsw_boot_cpuid:
	.word 0

.previous
