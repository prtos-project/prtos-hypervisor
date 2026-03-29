/*
 * FreeRTOS Kernel V10.0.1
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * port.c for RISC-V 64-bit (S-mode / VS-mode)
 *
 * This port runs FreeRTOS in S-mode on RISC-V. It uses:
 * - sstatus.SIE for global interrupt enable/disable
 * - SBI set_timer for tick timer
 * - sip.SSIP software interrupt for portYIELD
 * - No GIC/priority masking (simplified critical sections)
 */

/* Standard includes. */
#include <stdlib.h>

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"

#ifndef configSETUP_TICK_INTERRUPT
	#error configSETUP_TICK_INTERRUPT() must be defined.
#endif

#ifndef configCLEAR_TICK_INTERRUPT
	#define configCLEAR_TICK_INTERRUPT()
#endif

/* A critical section is exited when the critical section nesting count reaches
this value. */
#define portNO_CRITICAL_NESTING			( ( size_t ) 0 )

/* Tasks are not created with a floating point context, but can be given a
floating point context after they have been created. */
#define portNO_FLOATING_POINT_CONTEXT	( ( StackType_t ) 0 )

/* RISC-V S-mode: sstatus.SIE is bit 1 */
#define portSSTATUS_SIE					( 1UL << 1 )
/* RISC-V S-mode: sstatus.SPIE is bit 5 */
#define portSSTATUS_SPIE				( 1UL << 5 )
/* RISC-V S-mode: sstatus.SPP is bit 8 (1 = S-mode) */
#define portSSTATUS_SPP					( 1UL << 8 )

/* Initial sstatus: SPP=1 (return to S-mode), SPIE=1 (enable interrupts on sret) */
#define portINITIAL_SSTATUS				( portSSTATUS_SPIE | portSSTATUS_SPP )

/* Dummy GIC registers - RISC-V doesn't use these but they satisfy link-time references */
volatile unsigned int riscv_dummy_pmr = 0xFF;
volatile unsigned int riscv_dummy_bpr = 0;
volatile unsigned int riscv_dummy_rpr = 0xFF;

/*-----------------------------------------------------------*/

/*
 * Starts the first task executing.  This function is necessarily written in
 * assembly code so is implemented in portASM.S.
 */
extern void vPortRestoreTaskContext( void );

/*-----------------------------------------------------------*/

/* A variable is used to keep track of the critical section nesting.  This
variable has to be stored as part of the task context and must be initialised to
a non zero value to ensure interrupts don't inadvertently become unmasked before
the scheduler starts.  As it is stored as part of the task context it will
automatically be set to 0 when the first task is started. */
volatile unsigned long ullCriticalNesting = 9999UL;

/* Saved as part of the task context.  If ullPortTaskHasFPUContext is non-zero
then floating point context must be saved and restored for the task. */
unsigned long ullPortTaskHasFPUContext = pdFALSE;

/* Set to 1 to pend a context switch from an ISR. */
unsigned long ullPortYieldRequired = pdFALSE;

/* Counts the interrupt nesting depth.  A context switch is only performed if
the nesting depth is 0. */
unsigned long ullPortInterruptNesting = 0;

/* Used in the ASM code - dummy values for RISC-V (GIC registers not used). */
__attribute__(( used )) const unsigned long ullICCEOIR = 0;
__attribute__(( used )) const unsigned long ullICCIAR = 0;
__attribute__(( used )) const unsigned long ullICCPMR = 0;
__attribute__(( used )) const unsigned long ullMaxAPIPriorityMask = 0;

/*-----------------------------------------------------------*/

/*
 * See header file for description.
 *
 * Stack layout (growing downward):
 *   [high address]
 *   x1 (ra)           <-- return address / link register
 *   x3 (gp)
 *   x4 (tp)
 *   x5-x7 (t0-t2)
 *   x8-x9 (s0-s1)
 *   x10-x17 (a0-a7)
 *   x18-x27 (s2-s11)
 *   x28-x31 (t3-t6)
 *   sepc              <-- task entry point
 *   sstatus           <-- initial processor state
 *   ullCriticalNesting
 *   ullPortTaskHasFPUContext
 *   [low address = pxTopOfStack]
 */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters )
{
	/* Setup the initial stack of the task.  The stack is set exactly as
	expected by the portRESTORE_CONTEXT() macro in portASM.S. */

	/* x1 (ra) - return address (unused for initial task, set to 0) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x3 (gp) - global pointer */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x4 (tp) - thread pointer */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x5 (t0) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x6 (t1) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x7 (t2) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x8 (s0/fp) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x9 (s1) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x10 (a0) - first argument = pvParameters */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pvParameters;
	/* x11 (a1) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x12 (a2) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x13 (a3) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x14 (a4) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x15 (a5) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x16 (a6) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x17 (a7) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x18 (s2) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x19 (s3) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x20 (s4) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x21 (s5) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x22 (s6) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x23 (s7) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x24 (s8) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x25 (s9) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x26 (s10) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x27 (s11) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x28 (t3) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x29 (t4) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x30 (t5) */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* x31 (t6) */
	pxTopOfStack--;
	*pxTopOfStack = 0;

	/* sepc - exception program counter = task entry point */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pxCode;

	/* sstatus - S-mode, interrupts enabled on sret (SPIE=1, SPP=1) */
	pxTopOfStack--;
	*pxTopOfStack = portINITIAL_SSTATUS;

	/* The task will start with a critical nesting count of 0 as interrupts are
	enabled. */
	pxTopOfStack--;
	*pxTopOfStack = portNO_CRITICAL_NESTING;

	/* The task will start without a floating point context. */
	pxTopOfStack--;
	*pxTopOfStack = portNO_FLOATING_POINT_CONTEXT;

	return pxTopOfStack;
}
/*-----------------------------------------------------------*/

BaseType_t xPortStartScheduler( void )
{
	/* Interrupts are turned off in the CPU itself to ensure a tick does
	not execute while the scheduler is being started.  Interrupts are
	automatically turned back on in the CPU when the first task starts
	executing (via sret restoring sstatus with SPIE set). */
	portDISABLE_INTERRUPTS();

	/* Start the timer that generates the tick ISR. */
	configSETUP_TICK_INTERRUPT();

	/* Start the first task executing. */
	vPortRestoreTaskContext();

	return 0;
}
/*-----------------------------------------------------------*/

void vPortEndScheduler( void )
{
	/* Not implemented in ports where there is nothing to return to. */
	configASSERT( ullCriticalNesting == 1000UL );
}
/*-----------------------------------------------------------*/

void vPortEnterCritical( void )
{
	/* Disable interrupts. */
	portDISABLE_INTERRUPTS();

	/* Now interrupts are disabled ullCriticalNesting can be accessed
	directly.  Increment ullCriticalNesting to keep a count of how many times
	portENTER_CRITICAL() has been called. */
	ullCriticalNesting++;

	/* This is not the interrupt safe version of the enter critical function so
	assert() if it is being called from an interrupt context. */
	if( ullCriticalNesting == 1UL )
	{
		configASSERT( ullPortInterruptNesting == 0 );
	}
}
/*-----------------------------------------------------------*/

void vPortExitCritical( void )
{
	if( ullCriticalNesting > portNO_CRITICAL_NESTING )
	{
		/* Decrement the nesting count as the critical section is being
		exited. */
		ullCriticalNesting--;

		/* If the nesting level has reached zero then all interrupt
		priorities must be re-enabled. */
		if( ullCriticalNesting == portNO_CRITICAL_NESTING )
		{
			portENABLE_INTERRUPTS();
		}
	}
}
/*-----------------------------------------------------------*/

void FreeRTOS_Tick_Handler( void )
{
	/* Set interrupt mask before altering scheduler structures.
	 * On RISC-V, we simply ensure interrupts are disabled. */
	portDISABLE_INTERRUPTS();

	/* Clear the tick interrupt and reprogram for next tick. */
	configCLEAR_TICK_INTERRUPT();

	/* Enable interrupts so higher-priority work can proceed. */
	portENABLE_INTERRUPTS();

	/* Increment the RTOS tick. */
	if( xTaskIncrementTick() != pdFALSE )
	{
		ullPortYieldRequired = pdTRUE;
	}
}
/*-----------------------------------------------------------*/

void vPortTaskUsesFPU( void )
{
	/* A task is registering the fact that it needs an FPU context. */
	ullPortTaskHasFPUContext = pdTRUE;
}
/*-----------------------------------------------------------*/

void vPortClearInterruptMask( UBaseType_t uxNewMaskValue )
{
	if( uxNewMaskValue == pdFALSE )
	{
		portENABLE_INTERRUPTS();
	}
}
/*-----------------------------------------------------------*/

UBaseType_t uxPortSetInterruptMask( void )
{
	unsigned long ulSstatus;

	/* Read current sstatus to check if interrupts were already disabled. */
	__asm volatile( "csrr %0, sstatus" : "=r"( ulSstatus ) :: "memory" );

	/* Disable interrupts. */
	portDISABLE_INTERRUPTS();

	if( ( ulSstatus & portSSTATUS_SIE ) == 0 )
	{
		/* Interrupts were already masked. */
		return pdTRUE;
	}

	return pdFALSE;
}
/*-----------------------------------------------------------*/

#if( configASSERT_DEFINED == 1 )

	void vPortValidateInterruptPriority( void )
	{
		/* RISC-V does not have GIC priority levels.
		 * This function is a no-op on RISC-V. */
	}

#endif /* configASSERT_DEFINED */
/*-----------------------------------------------------------*/
