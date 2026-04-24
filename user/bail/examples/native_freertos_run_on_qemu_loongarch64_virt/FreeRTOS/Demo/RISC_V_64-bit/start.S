/*
 * start.S - Boot code for FreeRTOS on LoongArch64 QEMU virt
 *
 * Entry point. Runs in PLV0 (or guest PLV0 under LVZ).
 * a0 = hartid (0), a1 = unused
 */

	.section .text.boot
	.globl _boot
_boot:
	/* Only CPU 0 proceeds; others loop */
	csrrd	$t0, 0x20		/* CSR.CPUID (CSR number 0x20) */
	bnez	$t0, _hang

	/* Set up stack pointer */
	la.pcrel $sp, stack_top

	/* Clear BSS */
	la.pcrel $t0, __bss_start
	la.pcrel $t1, __bss_end
1:
	bge	$t0, $t1, 2f
	st.d	$zero, $t0, 0
	addi.d	$t0, $t0, 8
	b	1b
2:
	/* Call main */
	bl	main

	/* Should not return */
_hang:
	idle	0
	b	_hang
