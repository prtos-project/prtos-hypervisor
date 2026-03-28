/*
 * FreeRTOS Kernel V10.0.1
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * portASM.S for RISC-V 64-bit (S-mode / VS-mode)
 *
 * Context save/restore macros and trap handlers for FreeRTOS on RISC-V.
 *
 * Stack frame layout (34 slots * 8 bytes = 272 bytes):
 *   offset 0:   ullPortTaskHasFPUContext
 *   offset 8:   ullCriticalNesting
 *   offset 16:  sstatus
 *   offset 24:  sepc (return address)
 *   offset 32:  x31 (t6)
 *   offset 40:  x30 (t5)
 *   offset 48:  x29 (t4)
 *   offset 56:  x28 (t3)
 *   offset 64:  x27 (s11)
 *   offset 72:  x26 (s10)
 *   offset 80:  x25 (s9)
 *   offset 88:  x24 (s8)
 *   offset 96:  x23 (s7)
 *   offset 104: x22 (s6)
 *   offset 112: x21 (s5)
 *   offset 120: x20 (s4)
 *   offset 128: x19 (s3)
 *   offset 136: x18 (s2)
 *   offset 144: x17 (a7)
 *   offset 152: x16 (a6)
 *   offset 160: x15 (a5)
 *   offset 168: x14 (a4)
 *   offset 176: x13 (a3)
 *   offset 184: x12 (a2)
 *   offset 192: x11 (a1)
 *   offset 200: x10 (a0)
 *   offset 208: x9  (s1)
 *   offset 216: x8  (s0/fp)
 *   offset 224: x7  (t2)
 *   offset 232: x6  (t1)
 *   offset 240: x5  (t0)
 *   offset 248: x4  (tp)
 *   offset 256: x3  (gp)
 *   offset 264: x1  (ra)
 */

#define portCONTEXT_SIZE  272

.macro portSAVE_CONTEXT
	/* Allocate stack frame */
	addi sp, sp, -portCONTEXT_SIZE

	/* Save general purpose registers (x1, x3-x31; x2=sp saved later) */
	sd x1,  264(sp)
	sd x3,  256(sp)
	sd x4,  248(sp)
	sd x5,  240(sp)
	sd x6,  232(sp)
	sd x7,  224(sp)
	sd x8,  216(sp)
	sd x9,  208(sp)
	sd x10, 200(sp)
	sd x11, 192(sp)
	sd x12, 184(sp)
	sd x13, 176(sp)
	sd x14, 168(sp)
	sd x15, 160(sp)
	sd x16, 152(sp)
	sd x17, 144(sp)
	sd x18, 136(sp)
	sd x19, 128(sp)
	sd x20, 120(sp)
	sd x21, 112(sp)
	sd x22, 104(sp)
	sd x23, 96(sp)
	sd x24, 88(sp)
	sd x25, 80(sp)
	sd x26, 72(sp)
	sd x27, 64(sp)
	sd x28, 56(sp)
	sd x29, 48(sp)
	sd x30, 40(sp)
	sd x31, 32(sp)

	/* Save sepc and sstatus */
	csrr t0, sepc
	sd t0, 24(sp)
	csrr t0, sstatus
	sd t0, 16(sp)

	/* Save critical nesting count */
	la t0, ullCriticalNesting
	ld t1, 0(t0)
	sd t1, 8(sp)

	/* Save FPU context flag */
	la t0, ullPortTaskHasFPUContext
	ld t1, 0(t0)
	sd t1, 0(sp)

	/* Save the stack pointer into the TCB */
	la t0, pxCurrentTCB
	ld t1, 0(t0)
	sd sp, 0(t1)
.endm

.macro portRESTORE_CONTEXT
	/* Load the stack pointer from the TCB */
	la t0, pxCurrentTCB
	ld t1, 0(t0)
	ld sp, 0(t1)

	/* Restore FPU context flag */
	ld t0, 0(sp)
	la t1, ullPortTaskHasFPUContext
	sd t0, 0(t1)

	/* Restore critical nesting count */
	ld t0, 8(sp)
	la t1, ullCriticalNesting
	sd t0, 0(t1)

	/* Restore sstatus and sepc */
	ld t0, 16(sp)
	csrw sstatus, t0
	ld t0, 24(sp)
	csrw sepc, t0

	/* Restore general purpose registers */
	ld x1,  264(sp)
	ld x3,  256(sp)
	ld x4,  248(sp)
	ld x5,  240(sp)
	ld x6,  232(sp)
	ld x7,  224(sp)
	ld x8,  216(sp)
	ld x9,  208(sp)
	ld x10, 200(sp)
	ld x11, 192(sp)
	ld x12, 184(sp)
	ld x13, 176(sp)
	ld x14, 168(sp)
	ld x15, 160(sp)
	ld x16, 152(sp)
	ld x17, 144(sp)
	ld x18, 136(sp)
	ld x19, 128(sp)
	ld x20, 120(sp)
	ld x21, 112(sp)
	ld x22, 104(sp)
	ld x23, 96(sp)
	ld x24, 88(sp)
	ld x25, 80(sp)
	ld x26, 72(sp)
	ld x27, 64(sp)
	ld x28, 56(sp)
	ld x29, 48(sp)
	ld x30, 40(sp)
	ld x31, 32(sp)

	/* Deallocate stack frame */
	addi sp, sp, portCONTEXT_SIZE

	/* Return from exception */
	sret
.endm

/*-----------------------------------------------------------*/

	.align 4
	.globl _freertos_trap_entry
_freertos_trap_entry:
	/* Save all registers on the current task's stack */
	addi sp, sp, -portCONTEXT_SIZE

	sd x1,  264(sp)
	sd x3,  256(sp)
	sd x4,  248(sp)
	sd x5,  240(sp)
	sd x6,  232(sp)
	sd x7,  224(sp)
	sd x8,  216(sp)
	sd x9,  208(sp)
	sd x10, 200(sp)
	sd x11, 192(sp)
	sd x12, 184(sp)
	sd x13, 176(sp)
	sd x14, 168(sp)
	sd x15, 160(sp)
	sd x16, 152(sp)
	sd x17, 144(sp)
	sd x18, 136(sp)
	sd x19, 128(sp)
	sd x20, 120(sp)
	sd x21, 112(sp)
	sd x22, 104(sp)
	sd x23, 96(sp)
	sd x24, 88(sp)
	sd x25, 80(sp)
	sd x26, 72(sp)
	sd x27, 64(sp)
	sd x28, 56(sp)
	sd x29, 48(sp)
	sd x30, 40(sp)
	sd x31, 32(sp)

	csrr t0, sepc
	sd t0, 24(sp)
	csrr t0, sstatus
	sd t0, 16(sp)

	la t0, ullCriticalNesting
	ld t1, 0(t0)
	sd t1, 8(sp)

	la t0, ullPortTaskHasFPUContext
	ld t1, 0(t0)
	sd t1, 0(sp)

	/* Save sp to TCB */
	la t0, pxCurrentTCB
	ld t1, 0(t0)
	sd sp, 0(t1)

	/* Read scause to determine trap type */
	csrr a0, scause

	/* Check if this is an interrupt (bit 63 set) */
	li t0, 1
	slli t0, t0, 63
	and t1, a0, t0
	beqz t1, _handle_exception

	/* It is an interrupt */
	/* Mask off the interrupt bit to get the cause code */
	not t0, t0
	and a0, a0, t0

	/* Check for software interrupt (cause 1) = portYIELD */
	li t0, 1
	beq a0, t0, _handle_yield_interrupt

	/* Check for timer interrupt (cause 5) */
	li t0, 5
	beq a0, t0, _handle_timer_interrupt

	/* Unknown interrupt - just restore and return */
	j _restore_context

_handle_yield_interrupt:
	/* Clear the software interrupt pending bit (sip.SSIP) */
	li t0, 2  /* bit 1 */
	csrc sip, t0

	/* Increment interrupt nesting */
	la t0, ullPortInterruptNesting
	ld t1, 0(t0)
	addi t1, t1, 1
	sd t1, 0(t0)

	/* Call vTaskSwitchContext */
	call vTaskSwitchContext

	/* Clear yield required flag */
	la t0, ullPortYieldRequired
	sd zero, 0(t0)

	/* Decrement interrupt nesting */
	la t0, ullPortInterruptNesting
	ld t1, 0(t0)
	addi t1, t1, -1
	sd t1, 0(t0)

	j _restore_context

_handle_timer_interrupt:
	/* Increment interrupt nesting */
	la t0, ullPortInterruptNesting
	ld t1, 0(t0)
	addi t1, t1, 1
	sd t1, 0(t0)

	/* Call the application IRQ handler (which calls FreeRTOS_Tick_Handler) */
	/* a0 already has the full scause value (5 = timer) */
	csrr a0, scause
	call vApplicationIRQHandler

	/* Check if a context switch is required */
	la t0, ullPortYieldRequired
	ld t1, 0(t0)
	beqz t1, _timer_no_switch

	/* Context switch required */
	sd zero, 0(t0)
	call vTaskSwitchContext

_timer_no_switch:
	/* Decrement interrupt nesting */
	la t0, ullPortInterruptNesting
	ld t1, 0(t0)
	addi t1, t1, -1
	sd t1, 0(t0)

	j _restore_context

_handle_exception:
	/* Synchronous exception - check for ecall (environment call) */
	/* scause == 8: ecall from U-mode
	 * scause == 9: ecall from S-mode (used for portYIELD alternative) */
	li t0, 9
	beq a0, t0, _handle_ecall

	/* Unknown exception - loop forever */
	j _unknown_trap

_handle_ecall:
	/* Advance sepc past the ecall instruction (4 bytes) */
	ld t0, 24(sp)   /* load saved sepc */
	addi t0, t0, 4
	sd t0, 24(sp)   /* store updated sepc */

	/* Increment interrupt nesting */
	la t0, ullPortInterruptNesting
	ld t1, 0(t0)
	addi t1, t1, 1
	sd t1, 0(t0)

	/* Treat as yield request */
	call vTaskSwitchContext

	/* Clear yield required flag */
	la t0, ullPortYieldRequired
	sd zero, 0(t0)

	/* Decrement interrupt nesting */
	la t0, ullPortInterruptNesting
	ld t1, 0(t0)
	addi t1, t1, -1
	sd t1, 0(t0)

	j _restore_context

_unknown_trap:
	csrr a0, scause
	csrr a1, sepc
	csrr a2, stval
	j _unknown_trap

_restore_context:
	/* Restore context from the (possibly new) current task */
	la t0, pxCurrentTCB
	ld t1, 0(t0)
	ld sp, 0(t1)

	/* Restore FPU context flag */
	ld t0, 0(sp)
	la t1, ullPortTaskHasFPUContext
	sd t0, 0(t1)

	/* Restore critical nesting count */
	ld t0, 8(sp)
	la t1, ullCriticalNesting
	sd t0, 0(t1)

	/* Restore sstatus and sepc */
	ld t0, 16(sp)
	csrw sstatus, t0
	ld t0, 24(sp)
	csrw sepc, t0

	/* Restore general purpose registers */
	ld x1,  264(sp)
	ld x3,  256(sp)
	ld x4,  248(sp)
	ld x5,  240(sp)
	ld x6,  232(sp)
	ld x7,  224(sp)
	ld x8,  216(sp)
	ld x9,  208(sp)
	ld x10, 200(sp)
	ld x11, 192(sp)
	ld x12, 184(sp)
	ld x13, 176(sp)
	ld x14, 168(sp)
	ld x15, 160(sp)
	ld x16, 152(sp)
	ld x17, 144(sp)
	ld x18, 136(sp)
	ld x19, 128(sp)
	ld x20, 120(sp)
	ld x21, 112(sp)
	ld x22, 104(sp)
	ld x23, 96(sp)
	ld x24, 88(sp)
	ld x25, 80(sp)
	ld x26, 72(sp)
	ld x27, 64(sp)
	ld x28, 56(sp)
	ld x29, 48(sp)
	ld x30, 40(sp)
	ld x31, 32(sp)

	addi sp, sp, portCONTEXT_SIZE

	sret

/*-----------------------------------------------------------*/

	.align 4
	.globl vPortRestoreTaskContext
vPortRestoreTaskContext:
	/* Install our trap vector */
	la t0, _freertos_trap_entry
	csrw stvec, t0

	/* Enable supervisor software interrupt (bit 1 of sie) for portYIELD.
	 * Timer interrupt (bit 5) is enabled in vConfigureTickInterrupt(). */
	li t0, (1 << 1)
	csrs sie, t0

	/* Restore the context of the first task */
	portRESTORE_CONTEXT

/*-----------------------------------------------------------*/

	.globl vPortInstallFreeRTOSVectorTable
vPortInstallFreeRTOSVectorTable:
	la t0, _freertos_trap_entry
	csrw stvec, t0
	ret
