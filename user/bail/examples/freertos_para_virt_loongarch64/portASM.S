/*
 * portASM.S - FreeRTOS context switch for LoongArch64 para-virt on PRTOS
 *
 * Unified full-context frame used by both voluntary yield and ISR.
 * Both paths return via IRET (syscall a0=44, a7=0) so the hypervisor
 * can restore ERA correctly.
 *
 * Frame layout (32 slots x 8 = 256 bytes, 16-byte aligned):
 *   Offset  Content
 *     0     ullCriticalNesting
 *     8     ra  ($r1)
 *    16     tp  ($r2)
 *    24     a0  ($r4)
 *    32     a1  ($r5)
 *    40     a2  ($r6)
 *    48     a3  ($r7)
 *    56     a4  ($r8)
 *    64     a5  ($r9)
 *    72     a6  ($r10)
 *    80     a7  ($r11)
 *    88     t0  ($r12)
 *    96     t1  ($r13)
 *   104     t2  ($r14)
 *   112     t3  ($r15)
 *   120     t4  ($r16)
 *   128     t5  ($r17)
 *   136     t6  ($r18)
 *   144     t7  ($r19)
 *   152     t8  ($r20)
 *   160     u0  ($r21)
 *   168     fp  ($r22)
 *   176     s0  ($r23)
 *   184     s1  ($r24)
 *   192     s2  ($r25)
 *   200     s3  ($r26)
 *   208     s4  ($r27)
 *   216     s5  ($r28)
 *   224     s6  ($r29)
 *   232     s7  ($r30)
 *   240     s8  ($r31)
 *   248     (padding)
 */

#define FRAME_SIZE   256

#define OFF_CRIT       0
#define OFF_RA         8
#define OFF_TP        16
#define OFF_A0        24
#define OFF_A1        32
#define OFF_A2        40
#define OFF_A3        48
#define OFF_A4        56
#define OFF_A5        64
#define OFF_A6        72
#define OFF_A7        80
#define OFF_T0        88
#define OFF_T1        96
#define OFF_T2       104
#define OFF_T3       112
#define OFF_T4       120
#define OFF_T5       128
#define OFF_T6       136
#define OFF_T7       144
#define OFF_T8       152
#define OFF_U0       160
#define OFF_FP       168
#define OFF_S0       176
#define OFF_S1       184
#define OFF_S2       192
#define OFF_S3       200
#define OFF_S4       208
#define OFF_S5       216
#define OFF_S6       224
#define OFF_S7       232
#define OFF_S8       240

/* PCT arch field offsets (from base of partition_control_table_t).
 * arch starts at offset 112; trap_entry at arch+32 = PCT+144 (verified). */
#define PCT_IRQ_SAVED_PC   112
#define PCT_IRQ_SAVED_A0   128
#define PCT_IRQ_VECTOR     136
#define PCT_IRQ_SAVED_A7   152

	.text

/*
 * SAVE_CONTEXT - save all GP registers, critical nesting, and SP to TCB.
 * Caller must have already done: addi.d $sp, $sp, -FRAME_SIZE
 */
.macro SAVE_CONTEXT
	st.d $ra,  $sp, OFF_RA
	st.d $tp,  $sp, OFF_TP
	st.d $a0,  $sp, OFF_A0
	st.d $a1,  $sp, OFF_A1
	st.d $a2,  $sp, OFF_A2
	st.d $a3,  $sp, OFF_A3
	st.d $a4,  $sp, OFF_A4
	st.d $a5,  $sp, OFF_A5
	st.d $a6,  $sp, OFF_A6
	st.d $a7,  $sp, OFF_A7
	st.d $t0,  $sp, OFF_T0
	st.d $t1,  $sp, OFF_T1
	st.d $t2,  $sp, OFF_T2
	st.d $t3,  $sp, OFF_T3
	st.d $t4,  $sp, OFF_T4
	st.d $t5,  $sp, OFF_T5
	st.d $t6,  $sp, OFF_T6
	st.d $t7,  $sp, OFF_T7
	st.d $t8,  $sp, OFF_T8
	st.d $r21, $sp, OFF_U0
	st.d $fp,  $sp, OFF_FP
	st.d $s0,  $sp, OFF_S0
	st.d $s1,  $sp, OFF_S1
	st.d $s2,  $sp, OFF_S2
	st.d $s3,  $sp, OFF_S3
	st.d $s4,  $sp, OFF_S4
	st.d $s5,  $sp, OFF_S5
	st.d $s6,  $sp, OFF_S6
	st.d $s7,  $sp, OFF_S7
	st.d $s8,  $sp, OFF_S8

	la.pcrel $t0, ullCriticalNesting
	ld.d $t1, $t0, 0
	st.d $t1, $sp, OFF_CRIT

	la.pcrel $t0, pxCurrentTCB
	ld.d $t1, $t0, 0
	st.d $sp, $t1, 0
.endm

/*
 * _do_restore_regs - Restore all GP registers from stack frame and IRET.
 * Expects $sp to point to a valid context frame.
 * PCT fields (irq_saved_pc etc.) must already be set by the caller.
 */
_do_restore_regs:
	ld.d $ra,  $sp, OFF_RA
	ld.d $tp,  $sp, OFF_TP
	ld.d $a0,  $sp, OFF_A0
	ld.d $a1,  $sp, OFF_A1
	ld.d $a2,  $sp, OFF_A2
	ld.d $a3,  $sp, OFF_A3
	ld.d $a4,  $sp, OFF_A4
	ld.d $a5,  $sp, OFF_A5
	ld.d $a6,  $sp, OFF_A6
	ld.d $a7,  $sp, OFF_A7
	ld.d $t0,  $sp, OFF_T0
	ld.d $t1,  $sp, OFF_T1
	ld.d $t2,  $sp, OFF_T2
	ld.d $t3,  $sp, OFF_T3
	ld.d $t4,  $sp, OFF_T4
	ld.d $t5,  $sp, OFF_T5
	ld.d $t6,  $sp, OFF_T6
	ld.d $t7,  $sp, OFF_T7
	ld.d $t8,  $sp, OFF_T8
	ld.d $r21, $sp, OFF_U0
	ld.d $fp,  $sp, OFF_FP
	ld.d $s0,  $sp, OFF_S0
	ld.d $s1,  $sp, OFF_S1
	ld.d $s2,  $sp, OFF_S2
	ld.d $s3,  $sp, OFF_S3
	ld.d $s4,  $sp, OFF_S4
	ld.d $s5,  $sp, OFF_S5
	ld.d $s6,  $sp, OFF_S6
	ld.d $s7,  $sp, OFF_S7
	ld.d $s8,  $sp, OFF_S8

	addi.d $sp, $sp, FRAME_SIZE

	/* IRET: hypervisor restores ERA from irq_saved_pc in PCT */
	li.w $a7, 0
	li.w $a0, 44
	syscall 0

/*
 * _yield_restore - Load new task from TCB, set PCT for IRET, restore regs.
 * Used after a context switch (yield, ISR with switch, first task start).
 */
_yield_restore:
	la.pcrel $t0, pxCurrentTCB
	ld.d $t1, $t0, 0
	ld.d $sp, $t1, 0

	ld.d $t1, $sp, OFF_CRIT
	la.pcrel $t0, ullCriticalNesting
	st.d $t1, $t0, 0

	/* Set PCT fields so IRET returns to the new task's ra */
	la.pcrel $t0, g_pct_addr
	ld.d $t0, $t0, 0
	ld.d $t1, $sp, OFF_RA
	st.d $t1, $t0, PCT_IRQ_SAVED_PC
	ld.d $t1, $sp, OFF_A0
	st.d $t1, $t0, PCT_IRQ_SAVED_A0
	ld.d $t1, $sp, OFF_A7
	st.d $t1, $t0, PCT_IRQ_SAVED_A7
	li.w $t1, 0x80000000
	st.w $t1, $t0, PCT_IRQ_VECTOR

	b _do_restore_regs

/*
 * _same_task_restore - Restore same task after ISR with no context switch.
 * irq_saved_pc was already set by fix_stack to the interrupted ERA.
 */
_same_task_restore:
	la.pcrel $t0, pxCurrentTCB
	ld.d $t1, $t0, 0
	ld.d $sp, $t1, 0

	ld.d $t1, $sp, OFF_CRIT
	la.pcrel $t0, ullCriticalNesting
	st.d $t1, $t0, 0

	b _do_restore_regs

/* -------------------------------------------------------------------- */

/*
 * vPortYield - Voluntary context switch.
 * Called as a regular function from task code (via portYIELD macro).
 */
	.align 4
	.globl vPortYield
vPortYield:
	addi.d $sp, $sp, -FRAME_SIZE
	SAVE_CONTEXT
	bl vTaskSwitchContext
	b _yield_restore

/*
 * _prtos_trap_dispatch - Virtual IRQ handler (ISR entry point).
 *
 * Called by hypervisor via fix_stack when a virtual IRQ is pending.
 * fix_stack has saved ERA/a0/a7 to PCT and set irq_vector.
 *
 * Saves full task context, calls FreeRTOS_Tick_Handler, optionally
 * does a context switch, then restores (possibly different) task.
 */
	.align 4
	.globl _prtos_trap_dispatch
_prtos_trap_dispatch:
	addi.d $sp, $sp, -FRAME_SIZE
	SAVE_CONTEXT

	bl FreeRTOS_Tick_Handler

	la.pcrel $t0, ullPortYieldRequired
	ld.d $t1, $t0, 0
	beqz $t1, _same_task_restore
	st.d $zero, $t0, 0
	bl vTaskSwitchContext
	b _yield_restore

/*
 * vPortRestoreTaskContext - Start the first task.
 * Called by xPortStartScheduler.
 *
 * Loads the first task's context, enables IRQ delivery, then IRET.
 * IRQ unmasking MUST happen here (not in xPortStartScheduler) because
 * the ISR's SAVE_CONTEXT would corrupt pxCurrentTCB->pxTopOfStack if
 * it fired while the scheduler's C stack was active.
 */
	.align 4
	.globl vPortRestoreTaskContext
vPortRestoreTaskContext:
	/* Load first task from TCB */
	la.pcrel $t0, pxCurrentTCB
	ld.d $t1, $t0, 0
	ld.d $sp, $t1, 0

	/* Restore critical nesting */
	ld.d $t1, $sp, OFF_CRIT
	la.pcrel $t0, ullCriticalNesting
	st.d $t1, $t0, 0

	/* Set PCT fields for IRET */
	la.pcrel $t0, g_pct_addr
	ld.d $t0, $t0, 0
	ld.d $t1, $sp, OFF_RA
	st.d $t1, $t0, PCT_IRQ_SAVED_PC
	ld.d $t1, $sp, OFF_A0
	st.d $t1, $t0, PCT_IRQ_SAVED_A0
	ld.d $t1, $sp, OFF_A7
	st.d $t1, $t0, PCT_IRQ_SAVED_A7
	li.w $t1, 0x80000000
	st.w $t1, $t0, PCT_IRQ_VECTOR

	/* Restore all GP registers (sp will be adjusted by _do_restore_regs) */
	ld.d $ra,  $sp, OFF_RA
	ld.d $tp,  $sp, OFF_TP
	ld.d $a0,  $sp, OFF_A0
	ld.d $a1,  $sp, OFF_A1
	ld.d $a2,  $sp, OFF_A2
	ld.d $a3,  $sp, OFF_A3
	ld.d $a4,  $sp, OFF_A4
	ld.d $a5,  $sp, OFF_A5
	ld.d $a6,  $sp, OFF_A6
	ld.d $a7,  $sp, OFF_A7
	ld.d $t0,  $sp, OFF_T0
	ld.d $t1,  $sp, OFF_T1
	ld.d $t2,  $sp, OFF_T2
	ld.d $t3,  $sp, OFF_T3
	ld.d $t4,  $sp, OFF_T4
	ld.d $t5,  $sp, OFF_T5
	ld.d $t6,  $sp, OFF_T6
	ld.d $t7,  $sp, OFF_T7
	ld.d $t8,  $sp, OFF_T8
	ld.d $r21, $sp, OFF_U0
	ld.d $fp,  $sp, OFF_FP
	ld.d $s0,  $sp, OFF_S0
	ld.d $s1,  $sp, OFF_S1
	ld.d $s2,  $sp, OFF_S2
	ld.d $s3,  $sp, OFF_S3
	ld.d $s4,  $sp, OFF_S4
	ld.d $s5,  $sp, OFF_S5
	ld.d $s6,  $sp, OFF_S6
	ld.d $s7,  $sp, OFF_S7
	ld.d $s8,  $sp, OFF_S8

	addi.d $sp, $sp, FRAME_SIZE

	/* Enable IRQ delivery (clear_irqmask: unmask timer ext_irq bit 0).
	 * irq_vector is already 0x80000000, so any ISR delivery is skipped. */
	li.w $a7, 0
	li.w $a0, 15		/* PRTOS_CLEAR_IRQMASK_NR */
	li.w $a1, 0
	li.w $a2, 1		/* unmask bit 0 */
	syscall 0

	/* IRET: hypervisor restores ERA from irq_saved_pc in PCT */
	li.w $a7, 0
	li.w $a0, 44
	syscall 0
