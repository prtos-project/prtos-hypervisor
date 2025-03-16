/*
 * FILE: boot.S
 *
 * Generic boot code, sets up a valid stack and jumps to the C code
 *
 * www.prtos.org
 */


.section .text.boot, "ax"
.global  _start
.type    _start, function
.align 4

_start:
    /* Set stack for c code */
    msr   DAIFSet, 0xf     /* Disable all interrupts */
    adrp    x0, _estack
    mov     sp, x0

    /* Get the current CPU ID */
    mrs     x1, mpidr_el1      /* Read MPIDR_EL1 (Multiprocessor Affinity Register) */
    and     x1, x1, #0x0F      /* Extract CPU ID (bits [3:0]) */

    /* Only CPU 0 is expected to run resident_sw code*/
    cbz     x1, rsw_main       /* If CPU ID == 0, jump to rsw_main */
    /* halted for other CPUs*/
1: 
    b     1b

.global  _halt_system
.type    _halt_system, function
.align 4
_halt_system:
1: 
    b     1b

.align 4
.bss
.align 4
.globl _stack
_stack:
	.zero (CONFIG_STACK_SIZE*1024)
.globl _estack
_estack:

.previous
