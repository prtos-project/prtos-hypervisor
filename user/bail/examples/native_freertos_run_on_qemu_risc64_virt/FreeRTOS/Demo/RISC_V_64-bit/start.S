/*
 * start.S - Boot code for FreeRTOS on RISC-V 64-bit QEMU virt
 *
 * Entry point from OpenSBI. Runs in S-mode.
 * a0 = hartid, a1 = DTB address
 */

	.section .text.boot
	.globl _boot
_boot:
	/* Only hart 0 proceeds; other harts loop */
	bnez a0, _hang

	/* Set up stack pointer */
	la sp, stack_top

	/* Clear BSS */
	la t0, __bss_start
	la t1, __bss_end
1:
	bge t0, t1, 2f
	sd zero, 0(t0)
	addi t0, t0, 8
	j 1b
2:
	/* Call main */
	call main

	/* Should not return */
_hang:
	wfi
	j _hang
