/*
 * FILE: boot.S
 *
 * RISC-V 64 RSW boot entry point
 * Sets up a valid stack and jumps to the C code
 *
 * www.prtos.org
 */

.section .text.boot, "ax"
.global  _start
.type    _start, function
.align 4

_start:
    /* Disable interrupts */
    csrci  sstatus, 0x2     /* Clear SIE bit */

    /* Set stack for C code */
    la     sp, _estack

    /*
     * OpenSBI passes hartid in a0.  With HSM only the boot hart
     * reaches S-mode; other harts are in STOPPED state.
     * Save boot hartid in global for PRTOS to pick up.
     */
    la     t1, rsw_boot_hartid
    sw     a0, 0(t1)

    j      rsw_main

.global  _halt_system
.type    _halt_system, function
.align 4
_halt_system:
1:
    wfi
    j      1b

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
.globl rsw_boot_hartid
rsw_boot_hartid:
	.word 0

.previous
