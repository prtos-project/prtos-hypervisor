/*
 * portASM.S for LoongArch64 (PLV0 / Guest mode via LVZ)
 *
 * Context save/restore and trap handler for FreeRTOS on LoongArch64.
 *
 * Stack frame layout (34 slots x 8 = 272 bytes):
 *   offset   0: ullPortTaskHasFPUContext
 *   offset   8: ullCriticalNesting
 *   offset  16: PRMD
 *   offset  24: ERA
 *   offset  32: ra  ($r1)
 *   offset  40: tp  ($r2)
 *   offset  48: a0  ($r4)
 *   offset  56: a1  ($r5)
 *   offset  64: a2  ($r6)
 *   offset  72: a3  ($r7)
 *   offset  80: a4  ($r8)
 *   offset  88: a5  ($r9)
 *   offset  96: a6  ($r10)
 *   offset 104: a7  ($r11)
 *   offset 112: t0  ($r12)
 *   offset 120: t1  ($r13)
 *   offset 128: t2  ($r14)
 *   offset 136: t3  ($r15)
 *   offset 144: t4  ($r16)
 *   offset 152: t5  ($r17)
 *   offset 160: t6  ($r18)
 *   offset 168: t7  ($r19)
 *   offset 176: t8  ($r20)
 *   offset 184: u0  ($r21)
 *   offset 192: fp  ($r22)
 *   offset 200: s0  ($r23)
 *   offset 208: s1  ($r24)
 *   offset 216: s2  ($r25)
 *   offset 224: s3  ($r26)
 *   offset 232: s4  ($r27)
 *   offset 240: s5  ($r28)
 *   offset 248: s6  ($r29)
 *   offset 256: s7  ($r30)
 *   offset 264: s8  ($r31)
 *
 * LoongArch64 CSR numbers:
 *   CRMD  = 0x0   (PLV, IE, DA, PG)
 *   PRMD  = 0x1   (PPLV, PIE - saved on exception, restored by ertn)
 *   ECFG  = 0x4   (LIE - per-line interrupt enable)
 *   ESTAT = 0x5   (IS - interrupt status, Ecode, EsubCode)
 *   ERA   = 0x6   (exception return address)
 *   BADV  = 0x7
 *   EENTRY = 0xC  (exception entry base address)
 *   TCFG  = 0x41  (timer config: En, Periodic, InitVal)
 *   TVAL  = 0x42  (timer remaining value)
 *   TICLR = 0x44  (timer interrupt clear)
 */

#define portCONTEXT_SIZE  272

/* CSR numbers */
#define CSR_CRMD    0x0
#define CSR_PRMD    0x1
#define CSR_ECFG    0x4
#define CSR_ESTAT   0x5
#define CSR_ERA     0x6
#define CSR_EENTRY  0xC
#define CSR_TICLR   0x44

.macro portSAVE_CONTEXT
	addi.d  $sp, $sp, -portCONTEXT_SIZE

	st.d  $r1,  $sp, 32    /* ra */
	st.d  $r2,  $sp, 40    /* tp */
	st.d  $r4,  $sp, 48    /* a0 */
	st.d  $r5,  $sp, 56    /* a1 */
	st.d  $r6,  $sp, 64    /* a2 */
	st.d  $r7,  $sp, 72    /* a3 */
	st.d  $r8,  $sp, 80    /* a4 */
	st.d  $r9,  $sp, 88    /* a5 */
	st.d  $r10, $sp, 96    /* a6 */
	st.d  $r11, $sp, 104   /* a7 */
	st.d  $r12, $sp, 112   /* t0 */
	st.d  $r13, $sp, 120   /* t1 */
	st.d  $r14, $sp, 128   /* t2 */
	st.d  $r15, $sp, 136   /* t3 */
	st.d  $r16, $sp, 144   /* t4 */
	st.d  $r17, $sp, 152   /* t5 */
	st.d  $r18, $sp, 160   /* t6 */
	st.d  $r19, $sp, 168   /* t7 */
	st.d  $r20, $sp, 176   /* t8 */
	st.d  $r21, $sp, 184   /* u0 */
	st.d  $r22, $sp, 192   /* fp */
	st.d  $r23, $sp, 200   /* s0 */
	st.d  $r24, $sp, 208   /* s1 */
	st.d  $r25, $sp, 216   /* s2 */
	st.d  $r26, $sp, 224   /* s3 */
	st.d  $r27, $sp, 232   /* s4 */
	st.d  $r28, $sp, 240   /* s5 */
	st.d  $r29, $sp, 248   /* s6 */
	st.d  $r30, $sp, 256   /* s7 */
	st.d  $r31, $sp, 264   /* s8 */

	/* Save ERA and PRMD */
	csrrd $t0, CSR_ERA
	st.d  $t0, $sp, 24
	csrrd $t0, CSR_PRMD
	st.d  $t0, $sp, 16

	/* Save critical nesting */
	la.pcrel $t0, ullCriticalNesting
	ld.d  $t1, $t0, 0
	st.d  $t1, $sp, 8

	/* Save FPU context flag */
	la.pcrel $t0, ullPortTaskHasFPUContext
	ld.d  $t1, $t0, 0
	st.d  $t1, $sp, 0

	/* Save sp to TCB */
	la.pcrel $t0, pxCurrentTCB
	ld.d  $t1, $t0, 0
	st.d  $sp, $t1, 0
.endm

.macro portRESTORE_CONTEXT
	/* Load sp from TCB */
	la.pcrel $t0, pxCurrentTCB
	ld.d  $t1, $t0, 0
	ld.d  $sp, $t1, 0

	/* Restore FPU context flag */
	ld.d  $t0, $sp, 0
	la.pcrel $t1, ullPortTaskHasFPUContext
	st.d  $t0, $t1, 0

	/* Restore critical nesting */
	ld.d  $t0, $sp, 8
	la.pcrel $t1, ullCriticalNesting
	st.d  $t0, $t1, 0

	/* Restore PRMD and ERA */
	ld.d  $t0, $sp, 16
	csrwr $t0, CSR_PRMD
	ld.d  $t0, $sp, 24
	csrwr $t0, CSR_ERA

	/* Restore general registers */
	ld.d  $r1,  $sp, 32
	ld.d  $r2,  $sp, 40
	ld.d  $r4,  $sp, 48
	ld.d  $r5,  $sp, 56
	ld.d  $r6,  $sp, 64
	ld.d  $r7,  $sp, 72
	ld.d  $r8,  $sp, 80
	ld.d  $r9,  $sp, 88
	ld.d  $r10, $sp, 96
	ld.d  $r11, $sp, 104
	ld.d  $r12, $sp, 112
	ld.d  $r13, $sp, 120
	ld.d  $r14, $sp, 128
	ld.d  $r15, $sp, 136
	ld.d  $r16, $sp, 144
	ld.d  $r17, $sp, 152
	ld.d  $r18, $sp, 160
	ld.d  $r19, $sp, 168
	ld.d  $r20, $sp, 176
	ld.d  $r21, $sp, 184
	ld.d  $r22, $sp, 192
	ld.d  $r23, $sp, 200
	ld.d  $r24, $sp, 208
	ld.d  $r25, $sp, 216
	ld.d  $r26, $sp, 224
	ld.d  $r27, $sp, 232
	ld.d  $r28, $sp, 240
	ld.d  $r29, $sp, 248
	ld.d  $r30, $sp, 256
	ld.d  $r31, $sp, 264

	addi.d $sp, $sp, portCONTEXT_SIZE

	ertn
.endm

/*-----------------------------------------------------------*/

	.text
	.align 4
	.globl _freertos_trap_entry
_freertos_trap_entry:
	addi.d  $sp, $sp, -portCONTEXT_SIZE

	st.d  $r1,  $sp, 32
	st.d  $r2,  $sp, 40
	st.d  $r4,  $sp, 48
	st.d  $r5,  $sp, 56
	st.d  $r6,  $sp, 64
	st.d  $r7,  $sp, 72
	st.d  $r8,  $sp, 80
	st.d  $r9,  $sp, 88
	st.d  $r10, $sp, 96
	st.d  $r11, $sp, 104
	st.d  $r12, $sp, 112
	st.d  $r13, $sp, 120
	st.d  $r14, $sp, 128
	st.d  $r15, $sp, 136
	st.d  $r16, $sp, 144
	st.d  $r17, $sp, 152
	st.d  $r18, $sp, 160
	st.d  $r19, $sp, 168
	st.d  $r20, $sp, 176
	st.d  $r21, $sp, 184
	st.d  $r22, $sp, 192
	st.d  $r23, $sp, 200
	st.d  $r24, $sp, 208
	st.d  $r25, $sp, 216
	st.d  $r26, $sp, 224
	st.d  $r27, $sp, 232
	st.d  $r28, $sp, 240
	st.d  $r29, $sp, 248
	st.d  $r30, $sp, 256
	st.d  $r31, $sp, 264

	csrrd $t0, CSR_ERA
	st.d  $t0, $sp, 24
	csrrd $t0, CSR_PRMD
	st.d  $t0, $sp, 16

	la.pcrel $t0, ullCriticalNesting
	ld.d  $t1, $t0, 0
	st.d  $t1, $sp, 8

	la.pcrel $t0, ullPortTaskHasFPUContext
	ld.d  $t1, $t0, 0
	st.d  $t1, $sp, 0

	/* Save sp to TCB */
	la.pcrel $t0, pxCurrentTCB
	ld.d  $t1, $t0, 0
	st.d  $sp, $t1, 0

	/* Read ESTAT to determine exception type */
	csrrd $a0, CSR_ESTAT

	/* Check for interrupts: ESTAT.IS (bits 12:0) ANDed with ECFG.LIE */
	csrrd $t0, CSR_ECFG
	and   $t1, $a0, $t0
	li.w  $t2, 0x1FFF       /* mask for IS bits 12:0 */
	and   $t1, $t1, $t2
	beqz  $t1, _handle_exception

	/* --- Interrupt handling --- */

	/* Check for software interrupt SWI0 (bit 0) = portYIELD */
	andi  $t2, $t1, 0x1
	bnez  $t2, _handle_yield_interrupt

	/* Check for timer interrupt TI (bit 11) */
	li.w  $t2, (1 << 11)
	and   $t2, $t1, $t2
	bnez  $t2, _handle_timer_interrupt

	/* Unknown interrupt - restore and return */
	b     _restore_context

_handle_yield_interrupt:
	/* Clear SWI0: read ESTAT, clear bit 0, write back */
	csrrd $t0, CSR_ESTAT
	li.d  $t1, ~(1 << 0)
	and   $t0, $t0, $t1
	csrwr $t0, CSR_ESTAT

	/* Increment interrupt nesting */
	la.pcrel $t0, ullPortInterruptNesting
	ld.d  $t1, $t0, 0
	addi.d $t1, $t1, 1
	st.d  $t1, $t0, 0

	bl    vTaskSwitchContext

	/* Clear yield required */
	la.pcrel $t0, ullPortYieldRequired
	st.d  $zero, $t0, 0

	/* Decrement interrupt nesting */
	la.pcrel $t0, ullPortInterruptNesting
	ld.d  $t1, $t0, 0
	addi.d $t1, $t1, -1
	st.d  $t1, $t0, 0

	b     _restore_context

_handle_timer_interrupt:
	/* Increment interrupt nesting */
	la.pcrel $t0, ullPortInterruptNesting
	ld.d  $t1, $t0, 0
	addi.d $t1, $t1, 1
	st.d  $t1, $t0, 0

	/* Call the application IRQ handler */
	csrrd $a0, CSR_ESTAT
	bl    vApplicationIRQHandler

	/* Check if context switch required */
	la.pcrel $t0, ullPortYieldRequired
	ld.d  $t1, $t0, 0
	beqz  $t1, _timer_no_switch

	st.d  $zero, $t0, 0
	bl    vTaskSwitchContext

_timer_no_switch:
	/* Decrement interrupt nesting */
	la.pcrel $t0, ullPortInterruptNesting
	ld.d  $t1, $t0, 0
	addi.d $t1, $t1, -1
	st.d  $t1, $t0, 0

	b     _restore_context

_handle_exception:
	/* Check Ecode (ESTAT bits 21:16) for syscall (Ecode = 0x0B) */
	srli.d $t0, $a0, 16
	andi  $t0, $t0, 0x3F
	li.w  $t1, 0x0B        /* Ecode for SYS (syscall) */
	beq   $t0, $t1, _handle_ecall

	/* Check Ecode for BRK (Ecode = 0x0C) - alternative yield */
	li.w  $t1, 0x0C
	beq   $t0, $t1, _handle_ecall

	/* Unknown exception - loop */
	b     _unknown_trap

_handle_ecall:
	/* Advance ERA past the syscall/break instruction (4 bytes) */
	ld.d  $t0, $sp, 24
	addi.d $t0, $t0, 4
	st.d  $t0, $sp, 24

	la.pcrel $t0, ullPortInterruptNesting
	ld.d  $t1, $t0, 0
	addi.d $t1, $t1, 1
	st.d  $t1, $t0, 0

	bl    vTaskSwitchContext

	la.pcrel $t0, ullPortYieldRequired
	st.d  $zero, $t0, 0

	la.pcrel $t0, ullPortInterruptNesting
	ld.d  $t1, $t0, 0
	addi.d $t1, $t1, -1
	st.d  $t1, $t0, 0

	b     _restore_context

_unknown_trap:
	csrrd $a0, CSR_ESTAT
	csrrd $a1, CSR_ERA
	b     _unknown_trap

_restore_context:
	/* Restore context from (possibly new) current task */
	la.pcrel $t0, pxCurrentTCB
	ld.d  $t1, $t0, 0
	ld.d  $sp, $t1, 0

	ld.d  $t0, $sp, 0
	la.pcrel $t1, ullPortTaskHasFPUContext
	st.d  $t0, $t1, 0

	ld.d  $t0, $sp, 8
	la.pcrel $t1, ullCriticalNesting
	st.d  $t0, $t1, 0

	ld.d  $t0, $sp, 16
	csrwr $t0, CSR_PRMD
	ld.d  $t0, $sp, 24
	csrwr $t0, CSR_ERA

	ld.d  $r1,  $sp, 32
	ld.d  $r2,  $sp, 40
	ld.d  $r4,  $sp, 48
	ld.d  $r5,  $sp, 56
	ld.d  $r6,  $sp, 64
	ld.d  $r7,  $sp, 72
	ld.d  $r8,  $sp, 80
	ld.d  $r9,  $sp, 88
	ld.d  $r10, $sp, 96
	ld.d  $r11, $sp, 104
	ld.d  $r12, $sp, 112
	ld.d  $r13, $sp, 120
	ld.d  $r14, $sp, 128
	ld.d  $r15, $sp, 136
	ld.d  $r16, $sp, 144
	ld.d  $r17, $sp, 152
	ld.d  $r18, $sp, 160
	ld.d  $r19, $sp, 168
	ld.d  $r20, $sp, 176
	ld.d  $r21, $sp, 184
	ld.d  $r22, $sp, 192
	ld.d  $r23, $sp, 200
	ld.d  $r24, $sp, 208
	ld.d  $r25, $sp, 216
	ld.d  $r26, $sp, 224
	ld.d  $r27, $sp, 232
	ld.d  $r28, $sp, 240
	ld.d  $r29, $sp, 248
	ld.d  $r30, $sp, 256
	ld.d  $r31, $sp, 264

	addi.d $sp, $sp, portCONTEXT_SIZE

	ertn

/*-----------------------------------------------------------*/

	.align 4
	.globl vPortRestoreTaskContext
vPortRestoreTaskContext:
	/* Install trap vector */
	la.pcrel $t0, _freertos_trap_entry
	csrwr $t0, CSR_EENTRY

	/* Enable SWI0 (bit 0) and TI (bit 11) in ECFG.LIE */
	li.w  $t0, (1 << 0) | (1 << 11)
	csrwr $t0, CSR_ECFG

	/* Restore first task context */
	portRESTORE_CONTEXT

/*-----------------------------------------------------------*/

	.globl vPortInstallFreeRTOSVectorTable
vPortInstallFreeRTOSVectorTable:
	la.pcrel $t0, _freertos_trap_entry
	csrwr $t0, CSR_EENTRY
	jirl  $zero, $ra, 0
