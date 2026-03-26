/******************************************************************************
*
* Copyright (C) 2014 Xilinx, Inc. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* XILINX CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/
/*****************************************************************************/
/**
* @file asm_vectors.s
*
* This file contains the initial vector table for the Cortex A53 processor
* Currently NEON registers are not saved on stack if interrupt is taken.
* It will be implemented.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who     Date     Changes
* ----- ------- -------- ---------------------------------------------------
* 5.00	pkp	5/21/14 Initial version
* </pre>
*
* @note
*
* None.
*
******************************************************************************/
#define ASM_FILE   1

#include "exception.h"

.org 0
.text

.globl _boot
.globl _vector_table
.globl _freertos_vector_table

.globl FIQInterrupt
.globl IRQInterrupt
.globl SErrorInterrupt
.globl SynchronousInterrupt


.macro build_trapframe exc_type
	/*
	 * store generic registers from (x29,x30) pair to (x1,x2) pair.
	 */
	stp	x29, x30, [sp, #-16]!
	stp	x27, x28, [sp, #-16]!
	stp	x25, x26, [sp, #-16]!
	stp	x23, x24, [sp, #-16]!
	stp	x21, x22, [sp, #-16]!
	stp	x19, x20, [sp, #-16]!
	stp	x17, x18, [sp, #-16]!
	stp	x15, x16, [sp, #-16]!
	stp	x13, x14, [sp, #-16]!
	stp	x11, x12, [sp, #-16]!
	stp	x9, x10, [sp, #-16]!
	stp	x7, x8, [sp, #-16]!
	stp	x5, x6, [sp, #-16]!
	stp	x3, x4, [sp, #-16]!
	stp	x1, x2, [sp, #-16]!
	/*
	 * Store (spsr, x0)
	 */
	mrs	x21, spsr_el1
	stp	x21, x0, [sp, #-16]!
	/*
	 * Allocate a room for sp_el0 and store elr
	 */
	mrs	x21, elr_el1
	stp	xzr, x21, [sp, #-16]!
	/*
	 * store exception type and esr
	 */
	mov	x21, #(\exc_type)
	mrs	x22, esr_el1
	stp	x21, x22, [sp, #-16]!
.endm

.macro store_traped_sp
	mrs	x21, sp_el0
	str	x21, [sp, #EXC_EXC_SP_OFFSET]
.endm

.macro call_common_trap_handler
	mov	x0, sp
	bl common_trap_handler
.endm

.macro store_nested_sp
	mov	x21, sp
	add	x21, x21, #EXC_FRAME_SIZE
	str	x21, [sp, #EXC_EXC_SP_OFFSET]
.endm

.macro restore_traped_sp
	ldr	x21, [sp, #EXC_EXC_SP_OFFSET]
	msr	sp_el0, x21
.endm

.macro restore_trapframe

	/*
	 * Drop exception type, esr,
	 */
	add	sp, sp, #16
	/*
	 * Drop exception stack pointer and restore elr_el1
	 */
	ldp	x21, x22, [sp], #16
	msr	elr_el1, x22

	/*
	 * Retore spsr and x0
	 */
	ldp	x21, x0, [sp], #16
	msr	spsr_el1, x21

	/*
	 * Restore generic registers from (x29,x30) pair to (x1,x2) pair.
	 */
	ldp	x1, x2, [sp], #16
	ldp	x3, x4, [sp], #16
	ldp	x5, x6, [sp], #16
	ldp	x7, x8, [sp], #16
	ldp	x9, x10, [sp], #16
	ldp	x11, x12, [sp], #16
	ldp	x13, x14, [sp], #16
	ldp	x15, x16, [sp], #16
	ldp	x17, x18, [sp], #16
	ldp	x19, x20, [sp], #16
	ldp	x21, x22, [sp], #16
	ldp	x23, x24, [sp], #16
	ldp	x25, x26, [sp], #16
	ldp	x27, x28, [sp], #16
	ldp	x29, x30, [sp], #16

	eret
.endm


	/*
	 * Exception vectors.
	 */
.balign 2048
.section .vectors
_vector_table:

.set	VBAR, _vector_table
	/*
	 * Current EL with SP0
	 */
.org VBAR
	/* b	_boot */
	b	_curr_el_sp0_sync		/* Synchronous */
.org (VBAR + 0x80)
	b	_curr_el_sp0_irq		/* IRQ/vIRQ */
.org (VBAR + 0x100)
	b	_curr_el_sp0_fiq		/* FIQ/vFIQ */
.org (VBAR + 0x180)
	b	_curr_el_sp0_serror		/* SError/vSError */

	/*
	 * Current EL with SPx
	 */
.org (VBAR + 0x200)
	b	_curr_el_spx_sync		/* Synchronous */
.org (VBAR + 0x280)
	b	_curr_el_spx_irq		/* IRQ/vIRQ */
.org (VBAR + 0x300)
	b	_curr_el_spx_fiq		/* FIQ/vFIQ */
.org (VBAR + 0x380)
	b	_curr_el_spx_serror		/* SError/vSError */

	/*
	 * Lower EL using AArch64
	 */
.org (VBAR + 0x400)
	b	_lower_el_aarch64_sync
.org (VBAR + 0x480)
	b	_lower_el_aarch64_irq
.org (VBAR + 0x500)
	b	_lower_el_aarch64_fiq
.org (VBAR + 0x580)
	b	_lower_el_aarch64_serror

	/*
	 * Lower EL using AArch32
	 */
.org (VBAR + 0x600)
	b	_lower_el_aarch32_sync
.org (VBAR + 0x680)
	b	_lower_el_aarch32_irq
.org (VBAR + 0x700)
	b	_lower_el_aarch32_fiq
.org (VBAR + 0x780)
	b	_lower_el_aarch32_serror

	text_align
_curr_el_sp0_sync:
	build_trapframe AARCH64_EXC_SYNC_SP0
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	text_align
_curr_el_sp0_irq:
	build_trapframe AARCH64_EXC_IRQ_SP0
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	text_align
_curr_el_sp0_fiq:
	build_trapframe AARCH64_EXC_FIQ_SP0
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	text_align
_curr_el_sp0_serror:
	build_trapframe AARCH64_EXC_SERR_SP0
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe


	text_align
_curr_el_spx_sync:
	build_trapframe AARCH64_EXC_SYNC_SPX
	store_nested_sp
	call_common_trap_handler
	restore_trapframe

	text_align
_curr_el_spx_irq:
	build_trapframe AARCH64_EXC_IRQ_SPX
	store_nested_sp
	call_common_trap_handler
	restore_trapframe

	text_align
_curr_el_spx_fiq:
	build_trapframe AARCH64_EXC_FIQ_SPX
	store_nested_sp
	call_common_trap_handler
	restore_trapframe

	text_align
_curr_el_spx_serror:
	build_trapframe AARCH64_EXC_SERR_SPX
	store_nested_sp
	call_common_trap_handler
	restore_trapframe


	text_align
_lower_el_aarch64_sync:
	build_trapframe AARCH64_EXC_SYNC_AARCH64
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	text_align
_lower_el_aarch64_irq:
	build_trapframe AARCH64_EXC_IRQ_AARCH64
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	text_align
_lower_el_aarch64_fiq:
	build_trapframe AARCH64_EXC_FIQ_AARCH64
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	text_align
_lower_el_aarch64_serror:
	build_trapframe AARCH64_EXC_SERR_AARCH64
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe


	text_align
_lower_el_aarch32_sync:
	build_trapframe AARCH64_EXC_SYNC_AARCH32
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	text_align
_lower_el_aarch32_irq:
	build_trapframe AARCH64_EXC_IRQ_AARCH32
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	text_align
_lower_el_aarch32_fiq:
	build_trapframe AARCH64_EXC_FIQ_AARCH32
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe

	text_align
_lower_el_aarch32_serror:
	build_trapframe AARCH64_EXC_SERR_AARCH32
	store_traped_sp
	call_common_trap_handler
	restore_traped_sp
	restore_trapframe


/******************************************************************************
 * Vector table to use when FreeRTOS is running.
 *****************************************************************************/
.set	FREERTOS_VBAR, (VBAR+0x2000)

.org(FREERTOS_VBAR)
_freertos_vector_table:
	b	FreeRTOS_SWI_Handler

.org (FREERTOS_VBAR + 0x80)
	b	FreeRTOS_IRQ_Handler

.org (FREERTOS_VBAR + 0x100)
	b	.

.org (FREERTOS_VBAR + 0x180)
	b	.

.org (FREERTOS_VBAR + 0x200)
	b	FreeRTOS_SWI_Handler

.org (FREERTOS_VBAR + 0x280)
	b	FreeRTOS_IRQ_Handler

.org (FREERTOS_VBAR + 0x300)
	b	.

.org (FREERTOS_VBAR + 0x380)
	b	.

.org (FREERTOS_VBAR + 0x400)
	b	.

.org (FREERTOS_VBAR + 0x480)
	b	.

.org (FREERTOS_VBAR + 0x500)
	b	.

.org (FREERTOS_VBAR + 0x580)
	b	.

.org (FREERTOS_VBAR + 0x600)
	b	.

.org (FREERTOS_VBAR + 0x680)
	b	.

.org (FREERTOS_VBAR + 0x700)
	b	.

.org (FREERTOS_VBAR + 0x780)
	b	.

.org (FREERTOS_VBAR + 0x800)




SynchronousInterruptHandler:
	stp	X0,X1, [sp,#-0x10]!
	stp	X2,X3, [sp,#-0x10]!
	stp	X4,X5, [sp,#-0x10]!
	stp	X6,X7, [sp,#-0x10]!
	stp	X8,X9, [sp,#-0x10]!
	stp	X10,X11, [sp,#-0x10]!
	stp	X12,X13, [sp,#-0x10]!
	stp	X14,X15, [sp,#-0x10]!
	stp	X16,X17, [sp,#-0x10]!
	stp	X18,X19, [sp,#-0x10]!
	stp	X29,X30, [sp,#-0x10]!

	bl	SynchronousInterrupt

	ldp	X29,X30, [sp], #0x10
	ldp	X18,X19, [sp], #0x10
	ldp	X16,X17, [sp], #0x10
	ldp	X14,X15, [sp], #0x10
	ldp	X12,X13, [sp], #0x10
	ldp	X10,X11, [sp], #0x10
	ldp	X8,X9, [sp], #0x10
	ldp	X6,X7, [sp], #0x10
	ldp	X4,X5, [sp], #0x10
	ldp	X2,X3, [sp], #0x10
	ldp	X0,X1, [sp], #0x10

	eret

IRQInterruptHandler:
	stp	X0,X1, [sp,#-0x10]!
	stp	X2,X3, [sp,#-0x10]!
	stp	X4,X5, [sp,#-0x10]!
	stp	X6,X7, [sp,#-0x10]!
	stp	X8,X9, [sp,#-0x10]!
	stp	X10,X11, [sp,#-0x10]!
	stp	X12,X13, [sp,#-0x10]!
	stp	X14,X15, [sp,#-0x10]!
	stp	X16,X17, [sp,#-0x10]!
	stp	X18,X19, [sp,#-0x10]!
	stp	X29,X30, [sp,#-0x10]!

	bl	IRQInterrupt

	ldp	X29,X30, [sp], #0x10
	ldp	X18,X19, [sp], #0x10
	ldp	X16,X17, [sp], #0x10
	ldp	X14,X15, [sp], #0x10
	ldp	X12,X13, [sp], #0x10
	ldp	X10,X11, [sp], #0x10
	ldp	X8,X9, [sp], #0x10
	ldp	X6,X7, [sp], #0x10
	ldp	X4,X5, [sp], #0x10
	ldp	X2,X3, [sp], #0x10
	ldp	X0,X1, [sp], #0x10

	eret

FIQInterruptHandler:

	stp	X0,X1, [sp,#-0x10]!
	stp	X2,X3, [sp,#-0x10]!
	stp	X4,X5, [sp,#-0x10]!
	stp	X6,X7, [sp,#-0x10]!
	stp	X8,X9, [sp,#-0x10]!
	stp	X10,X11, [sp,#-0x10]!
	stp	X12,X13, [sp,#-0x10]!
	stp	X14,X15, [sp,#-0x10]!
	stp	X16,X17, [sp,#-0x10]!
	stp	X18,X19, [sp,#-0x10]!
	stp	X29,X30, [sp,#-0x10]!

	bl	FIQInterrupt

	ldp	X29,X30, [sp], #0x10
	ldp	X18,X19, [sp], #0x10
	ldp	X16,X17, [sp], #0x10
	ldp	X14,X15, [sp], #0x10
	ldp	X12,X13, [sp], #0x10
	ldp	X10,X11, [sp], #0x10
	ldp	X8,X9, [sp], #0x10
	ldp	X6,X7, [sp], #0x10
	ldp	X4,X5, [sp], #0x10
	ldp	X2,X3, [sp], #0x10
	ldp	X0,X1, [sp], #0x10

	eret

SErrorInterruptHandler:

	stp	X0,X1, [sp,#-0x10]!
	stp	X2,X3, [sp,#-0x10]!
	stp	X4,X5, [sp,#-0x10]!
	stp	X6,X7, [sp,#-0x10]!
	stp	X8,X9, [sp,#-0x10]!
	stp	X10,X11, [sp,#-0x10]!
	stp	X12,X13, [sp,#-0x10]!
	stp	X14,X15, [sp,#-0x10]!
	stp	X16,X17, [sp,#-0x10]!
	stp	X18,X19, [sp,#-0x10]!
	stp	X29,X30, [sp,#-0x10]!

	bl	SErrorInterrupt

	ldp	X29,X30, [sp], #0x10
	ldp	X18,X19, [sp], #0x10
	ldp	X16,X17, [sp], #0x10
	ldp	X14,X15, [sp], #0x10
	ldp	X12,X13, [sp], #0x10
	ldp	X10,X11, [sp], #0x10
	ldp	X8,X9, [sp], #0x10
	ldp	X6,X7, [sp], #0x10
	ldp	X4,X5, [sp], #0x10
	ldp	X2,X3, [sp], #0x10
	ldp	X0,X1, [sp], #0x10

	eret

.end
