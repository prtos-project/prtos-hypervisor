/*
 * FreeRTOS on PRTOS - Hardware-Assisted Virtualization
 * Entry point for EL1 guest partition.
 * PRTOS enters the partition at EL1h with MMU off.
 */

.globl	_boot
_boot:
	/* We are at EL1. Set up VBAR_EL1. */
	adr	x0, _vector_table
	msr	vbar_el1, x0

	/* Enable FP/SIMD at EL1 */
	mov	x0, #3 << 20
	msr	cpacr_el1, x0
	isb

	/* Check CPU ID = 0, or hang */
	mrs	x0, mpidr_el1
	and	x0, x0, #3
	cmp	x0, #0
	bne	hang

master_cpu:
	/* Configure stack */
	adrp	x0, stack_top
magic_label:
	mov	sp, x0

	/* Clear BSS */
	ldr	x1, =__bss_start
	ldr	w2, =__bss_size
1:	cbz	w2, 2f
	str	xzr, [x1], #8
	sub	w2, w2, #1
	cbnz	w2, 1b

2:	bl	main

hang:
	wfi
	b	hang
