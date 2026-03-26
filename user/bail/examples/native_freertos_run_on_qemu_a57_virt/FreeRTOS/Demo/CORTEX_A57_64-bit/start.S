/*
 * Branch according to exception level
 */
.macro	switch_el, xreg, el3_label, el2_label, el1_label
	mrs	\xreg, CurrentEL
	cmp	\xreg, 0xc
	b.eq	\el3_label
	cmp	\xreg, 0x8
	b.eq	\el2_label
	cmp	\xreg, 0x4
	b.eq	\el1_label
.endm

.globl	_boot
_boot:
	/*
	 * Could be EL3/EL2/EL1, Initial State:
	 * Little Endian, MMU Disabled, i/dCache Disabled
	 */
	adr	x0, _vector_table
	switch_el x1, 3f, 2f, 1f
3:	msr	vbar_el3, x0
	mrs	x0, scr_el3
	orr	x0, x0, #0xf			/* SCR_EL3.NS|IRQ|FIQ|EA */
	msr	scr_el3, x0
	msr	cptr_el3, xzr			/* Enable FP/SIMD */
	b	0f
2:	msr	vbar_el2, x0
	mov	x0, #0x33ff
	msr	cptr_el2, x0			/* Enable FP/SIMD */
	b	0f
1:	msr	vbar_el1, x0
	mov	x0, #3 << 20
	msr	cpacr_el1, x0			/* Enable FP/SIMD */
0:
	/* check CPU ID = 0x0, or jump to hang */
	mrs	x0, mpidr_el1
	and	x0, x0, #3 
	cmp	x0, #0
	bne	hang

master_cpu:
	/* configure stack */
	adrp	x0, stack_top	// Address of 4KB page at a PC-relative offset
magic_label:				// Why do we need this label to let GDB step continually?
	mov	sp, x0				// sp = stack_top (align with 4KB page)
	/* clear bss. */
	ldr	x1, =__bss_start	/* A 64-bit general-purpose register named X0 to X30 */
	ldr	w2, =__bss_size		/* A 32-bit general-purpose register named W0 to W30 */
1:	cbz	w2,	2f				/* Compare and Branch on Zero */
    str   xzr, [x1], #8
    sub   w2, w2, #1
    cbnz  w2, 1b

2:	bl	main

hang:
	wfi
	b	hang






# =========================================
	.align 3

#if 0 //RyanYao
.globl	_TEXT_BASE
_TEXT_BASE:
	.quad	CONFIG_SYS_TEXT_BASE

/*
 * These are defined in the linker script.
 */
.globl	_end_ofs
_end_ofs:
	.quad	_end - _start

.globl	_bss_start_ofs
_bss_start_ofs:
	.quad	__bss_start - _start

.globl	_bss_end_ofs
_bss_end_ofs:
	.quad	__bss_end - _start
#endif //RyanYao
