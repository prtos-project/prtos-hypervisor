/*
 * FreeRTOS portASM.S for AArch64 on PRTOS (HW-Virt)
 *
 * Based on FreeRTOS Kernel V10.0.1 GCC/ARM_CA57_64_BIT.
 * Modified to use GICv3 ICC_* system registers instead of MMIO:
 *   ICC_IAR1_EL1  = S3_0_C12_C12_0  (read interrupt ID)
 *   ICC_EOIR1_EL1 = S3_0_C12_C12_1  (end of interrupt)
 *   ICC_PMR_EL1   = S3_0_C4_C6_0    (priority mask)
 */

	.text

	/* Variables and functions. */
	.extern ullMaxAPIPriorityMask
	.extern pxCurrentTCB
	.extern vTaskSwitchContext
	.extern vApplicationIRQHandler
	.extern ullPortInterruptNesting
	.extern ullPortTaskHasFPUContext
	.extern ullCriticalNesting
	.extern ullPortYieldRequired
	.extern _freertos_vector_table

	.global FreeRTOS_IRQ_Handler
	.global FreeRTOS_SWI_Handler
	.global vPortRestoreTaskContext


.macro portSAVE_CONTEXT

	/* Switch to use the EL0 stack pointer. */
	MSR 	SPSEL, #0

	/* Save the entire context. */
	STP 	X0, X1, [SP, #-0x10]!
	STP 	X2, X3, [SP, #-0x10]!
	STP 	X4, X5, [SP, #-0x10]!
	STP 	X6, X7, [SP, #-0x10]!
	STP 	X8, X9, [SP, #-0x10]!
	STP 	X10, X11, [SP, #-0x10]!
	STP 	X12, X13, [SP, #-0x10]!
	STP 	X14, X15, [SP, #-0x10]!
	STP 	X16, X17, [SP, #-0x10]!
	STP 	X18, X19, [SP, #-0x10]!
	STP 	X20, X21, [SP, #-0x10]!
	STP 	X22, X23, [SP, #-0x10]!
	STP 	X24, X25, [SP, #-0x10]!
	STP 	X26, X27, [SP, #-0x10]!
	STP 	X28, X29, [SP, #-0x10]!
	STP 	X30, XZR, [SP, #-0x10]!

	/* Save the SPSR and ELR. */
	MRS		X3, SPSR_EL1
	MRS		X2, ELR_EL1

	STP 	X2, X3, [SP, #-0x10]!

	/* Save the critical section nesting depth. */
	LDR		X0, ullCriticalNestingConst
	LDR		X3, [X0]

	/* Save the FPU context indicator. */
	LDR		X0, ullPortTaskHasFPUContextConst
	LDR		X2, [X0]

	/* Save the FPU context, if any (32 128-bit registers). */
	CMP		X2, #0
	B.EQ	1f
	STP		Q0, Q1, [SP,#-0x20]!
	STP		Q2, Q3, [SP,#-0x20]!
	STP		Q4, Q5, [SP,#-0x20]!
	STP		Q6, Q7, [SP,#-0x20]!
	STP		Q8, Q9, [SP,#-0x20]!
	STP		Q10, Q11, [SP,#-0x20]!
	STP		Q12, Q13, [SP,#-0x20]!
	STP		Q14, Q15, [SP,#-0x20]!
	STP		Q16, Q17, [SP,#-0x20]!
	STP		Q18, Q19, [SP,#-0x20]!
	STP		Q20, Q21, [SP,#-0x20]!
	STP		Q22, Q23, [SP,#-0x20]!
	STP		Q24, Q25, [SP,#-0x20]!
	STP		Q26, Q27, [SP,#-0x20]!
	STP		Q28, Q29, [SP,#-0x20]!
	STP		Q30, Q31, [SP,#-0x20]!

1:
	/* Store the critical nesting count and FPU context indicator. */
	STP 	X2, X3, [SP, #-0x10]!

	LDR 	X0, pxCurrentTCBConst
	LDR 	X1, [X0]
	MOV 	X0, SP   /* Move SP into X0 for saving. */
	STR 	X0, [X1]

	/* Switch to use the ELx stack pointer. */
	MSR 	SPSEL, #1

	.endm

; /**********************************************************************/

.macro portRESTORE_CONTEXT

	/* Switch to use the EL0 stack pointer. */
	MSR 	SPSEL, #0

	/* Set the SP to point to the stack of the task being restored. */
	LDR		X0, pxCurrentTCBConst
	LDR		X1, [X0]
	LDR		X0, [X1]
	MOV		SP, X0

	LDP 	X2, X3, [SP], #0x10  /* Critical nesting and FPU context. */

	/* Set the PMR register to be correct for the current critical nesting
	depth. Uses ICC_PMR_EL1 system register instead of MMIO. */
	LDR		X0, ullCriticalNestingConst
	MOV		X1, #255					/* Unmask value. */
	CMP		X3, #0
	B.EQ	1f
	LDR		X6, ullMaxAPIPriorityMaskConst
	LDR		X1, [X6]					/* Mask value. */
1:
	MSR		S3_0_C4_C6_0, X1			/* Write ICC_PMR_EL1 */
	DSB 	SY
	ISB 	SY
	STR		X3, [X0]					/* Restore critical nesting count. */

	/* Restore the FPU context indicator. */
	LDR		X0, ullPortTaskHasFPUContextConst
	STR		X2, [X0]

	/* Restore the FPU context, if any. */
	CMP		X2, #0
	B.EQ	1f
	LDP		Q30, Q31, [SP], #0x20
	LDP		Q28, Q29, [SP], #0x20
	LDP		Q26, Q27, [SP], #0x20
	LDP		Q24, Q25, [SP], #0x20
	LDP		Q22, Q23, [SP], #0x20
	LDP		Q20, Q21, [SP], #0x20
	LDP		Q18, Q19, [SP], #0x20
	LDP		Q16, Q17, [SP], #0x20
	LDP		Q14, Q15, [SP], #0x20
	LDP		Q12, Q13, [SP], #0x20
	LDP		Q10, Q11, [SP], #0x20
	LDP		Q8, Q9, [SP], #0x20
	LDP		Q6, Q7, [SP], #0x20
	LDP		Q4, Q5, [SP], #0x20
	LDP		Q2, Q3, [SP], #0x20
	LDP		Q0, Q1, [SP], #0x20
1:
	LDP 	X2, X3, [SP], #0x10  /* SPSR and ELR. */

	/* Restore the SPSR. */
	MSR		SPSR_EL1, X3
	/* Restore the ELR. */
	MSR		ELR_EL1, X2

	LDP 	X30, XZR, [SP], #0x10
	LDP 	X28, X29, [SP], #0x10
	LDP 	X26, X27, [SP], #0x10
	LDP 	X24, X25, [SP], #0x10
	LDP 	X22, X23, [SP], #0x10
	LDP 	X20, X21, [SP], #0x10
	LDP 	X18, X19, [SP], #0x10
	LDP 	X16, X17, [SP], #0x10
	LDP 	X14, X15, [SP], #0x10
	LDP 	X12, X13, [SP], #0x10
	LDP 	X10, X11, [SP], #0x10
	LDP 	X8, X9, [SP], #0x10
	LDP 	X6, X7, [SP], #0x10
	LDP 	X4, X5, [SP], #0x10
	LDP 	X2, X3, [SP], #0x10
	LDP 	X0, X1, [SP], #0x10

	/* Switch to use the ELx stack pointer. */
	MSR 	SPSEL, #1

	ERET

	.endm


/******************************************************************************
 * FreeRTOS_SWI_Handler handler is used to perform a context switch.
 *****************************************************************************/
.align 8
.type FreeRTOS_SWI_Handler, %function
FreeRTOS_SWI_Handler:
	/* Save the context of the current task and select a new task to run. */
	portSAVE_CONTEXT
	MRS		X0, ESR_EL1

	LSR		X1, X0, #26

	CMP		X1, #0x15 	/* 0x15 = SVC instruction. */
	B.NE	FreeRTOS_Abort
	BL 		vTaskSwitchContext

	portRESTORE_CONTEXT

FreeRTOS_Abort:
	/* Full ESR is in X0, exception class code is in X1. */
	B		.

/******************************************************************************
 * vPortRestoreTaskContext is used to start the scheduler.
 *****************************************************************************/
.align 8
.type vPortRestoreTaskContext, %function
vPortRestoreTaskContext:
.set freertos_vector_base,	_freertos_vector_table

	/* Install the FreeRTOS interrupt handlers. */
	LDR		X1, =freertos_vector_base
	MSR		VBAR_EL1, X1
	DSB		SY
	ISB		SY

	/* Start the first task. */
	portRESTORE_CONTEXT


/******************************************************************************
 * FreeRTOS_IRQ_Handler handles IRQ entry and exit.
 * Uses ICC_IAR1_EL1 / ICC_EOIR1_EL1 system registers instead of MMIO.
 *****************************************************************************/
.align 8
.type FreeRTOS_IRQ_Handler, %function
FreeRTOS_IRQ_Handler:
	/* Save volatile registers. */
	STP		X0, X1, [SP, #-0x10]!
	STP		X2, X3, [SP, #-0x10]!
	STP		X4, X5, [SP, #-0x10]!
	STP		X6, X7, [SP, #-0x10]!
	STP		X8, X9, [SP, #-0x10]!
	STP		X10, X11, [SP, #-0x10]!
	STP		X12, X13, [SP, #-0x10]!
	STP		X14, X15, [SP, #-0x10]!
	STP		X16, X17, [SP, #-0x10]!
	STP		X18, X19, [SP, #-0x10]!
	STP		X29, X30, [SP, #-0x10]!

	/* Save the SPSR and ELR. */
	MRS		X3, SPSR_EL1
	MRS		X2, ELR_EL1
	STP 	X2, X3, [SP, #-0x10]!

	/* Increment the interrupt nesting counter. */
	LDR		X5, ullPortInterruptNestingConst
	LDR		X1, [X5]	/* Old nesting count in X1. */
	ADD		X6, X1, #1
	STR		X6, [X5]	/* Address of nesting count variable in X5. */

	/* Maintain the interrupt nesting information across the function call. */
	STP		X1, X5, [SP, #-0x10]!

	/* Read ICC_IAR1_EL1 to acknowledge the interrupt. */
	MRS		X0, S3_0_C12_C12_0		/* ICC_IAR1_EL1 → W0 */

	/* Maintain the ICCIAR value across the function call. */
	STP		X0, X1, [SP, #-0x10]!

	/* Call the C handler. */
	BL vApplicationIRQHandler

	/* Disable interrupts. */
	MSR 	DAIFSET, #2
	DSB		SY
	ISB		SY

	/* Restore the ICCIAR value. */
	LDP		X0, X1, [SP], #0x10

	/* Write ICC_EOIR1_EL1 to signal end of interrupt. */
	MSR		S3_0_C12_C12_1, X0		/* ICC_EOIR1_EL1 */

	/* Restore the critical nesting count. */
	LDP		X1, X5, [SP], #0x10
	STR		X1, [X5]

	/* Has interrupt nesting unwound? */
	CMP		X1, #0
	B.NE	Exit_IRQ_No_Context_Switch

	/* Is a context switch required? */
	LDR		X0, ullPortYieldRequiredConst
	LDR		X1, [X0]
	CMP		X1, #0
	B.EQ	Exit_IRQ_No_Context_Switch

	/* Reset ullPortYieldRequired to 0. */
	MOV		X2, #0
	STR		X2, [X0]

	/* Restore volatile registers. */
	LDP 	X4, X5, [SP], #0x10  /* SPSR and ELR. */
	MSR		SPSR_EL1, X5
	MSR		ELR_EL1, X4
	DSB		SY
	ISB		SY

	LDP		X29, X30, [SP], #0x10
	LDP		X18, X19, [SP], #0x10
	LDP		X16, X17, [SP], #0x10
	LDP		X14, X15, [SP], #0x10
	LDP		X12, X13, [SP], #0x10
	LDP		X10, X11, [SP], #0x10
	LDP		X8, X9, [SP], #0x10
	LDP		X6, X7, [SP], #0x10
	LDP		X4, X5, [SP], #0x10
	LDP		X2, X3, [SP], #0x10
	LDP		X0, X1, [SP], #0x10

	/* Save the context of the current task and select a new task to run. */
	portSAVE_CONTEXT
	BL vTaskSwitchContext
	portRESTORE_CONTEXT

Exit_IRQ_No_Context_Switch:
	/* Restore volatile registers. */
	LDP 	X4, X5, [SP], #0x10  /* SPSR and ELR. */
	MSR		SPSR_EL1, X5
	MSR		ELR_EL1, X4
	DSB		SY
	ISB		SY

	LDP		X29, X30, [SP], #0x10
	LDP		X18, X19, [SP], #0x10
	LDP		X16, X17, [SP], #0x10
	LDP		X14, X15, [SP], #0x10
	LDP		X12, X13, [SP], #0x10
	LDP		X10, X11, [SP], #0x10
	LDP		X8, X9, [SP], #0x10
	LDP		X6, X7, [SP], #0x10
	LDP		X4, X5, [SP], #0x10
	LDP		X2, X3, [SP], #0x10
	LDP		X0, X1, [SP], #0x10

	ERET



.align 8
pxCurrentTCBConst: .dword pxCurrentTCB
ullCriticalNestingConst: .dword ullCriticalNesting
ullPortTaskHasFPUContextConst: .dword ullPortTaskHasFPUContext

ullMaxAPIPriorityMaskConst: .dword ullMaxAPIPriorityMask
ullPortInterruptNestingConst: .dword ullPortInterruptNesting
ullPortYieldRequiredConst: .dword ullPortYieldRequired



.end
