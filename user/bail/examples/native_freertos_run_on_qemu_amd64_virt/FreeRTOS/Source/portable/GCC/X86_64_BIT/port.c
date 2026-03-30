/*
 * port.c for x86-64 (native, bare-metal)
 *
 * FreeRTOS port using cli/sti interrupt control.
 * Context is saved/restored in portASM.S.
 */
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

#ifndef configSETUP_TICK_INTERRUPT
	#error configSETUP_TICK_INTERRUPT() must be defined.
#endif

#ifndef configCLEAR_TICK_INTERRUPT
	#define configCLEAR_TICK_INTERRUPT()
#endif

#define portNO_CRITICAL_NESTING			( ( size_t ) 0 )
#define portNO_FLOATING_POINT_CONTEXT	( ( StackType_t ) 0 )

/* Initial RFLAGS: IF=1 (enable interrupts on iretq), bit 1 always set */
#define portINITIAL_RFLAGS				( 0x202UL )

/* Dummy GIC registers (not used on x86, but satisfy link references) */
volatile unsigned int x86_dummy_pmr = 0xFF;
volatile unsigned int x86_dummy_bpr = 0;
volatile unsigned int x86_dummy_rpr = 0xFF;

extern void vPortRestoreTaskContext( void );

volatile unsigned long ullCriticalNesting = 9999UL;
unsigned long ullPortTaskHasFPUContext = pdFALSE;
unsigned long ullPortYieldRequired = pdFALSE;
unsigned long ullPortInterruptNesting = 0;

/* Dummy GIC addresses for link compatibility */
__attribute__(( used )) const unsigned long ullICCEOIR = 0;
__attribute__(( used )) const unsigned long ullICCIAR = 0;
__attribute__(( used )) const unsigned long ullICCPMR = 0;
__attribute__(( used )) const unsigned long ullMaxAPIPriorityMask = 0;

/*
 * Stack layout (growing downward, 20 quadwords):
 *   ------ Interrupt frame (pushed by CPU on interrupt, or faked for initial task) ------
 *   SS               (selector 0x10)
 *   RSP              (task's real stack pointer)
 *   RFLAGS           (initial = 0x202, IF=1)
 *   CS               (selector 0x08)
 *   RIP              (task entry point)
 *   ------ Context frame (portCONTEXT_SIZE = 152 bytes) ------
 *   r15, r14, r13, r12, r11, r10, r9, r8
 *   rdi, rsi, rbp, rbx, rdx, rcx, rax
 *   rip (copy from interrupt frame)
 *   rflags (copy from interrupt frame)
 *   ullCriticalNesting
 *   ullPortTaskHasFPUContext
 */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters )
{
	/* Save the original top-of-stack for the fake RSP in interrupt frame.
	 * Subtract 1 (8 bytes) for x86-64 ABI alignment: at function entry
	 * RSP must be ≡ 8 mod 16. */
	StackType_t *pxTaskStackTop = pxTopOfStack - 1;

	/* --- Fake interrupt frame (5 items, 40 bytes) --- */
	/* SS */
	pxTopOfStack--;
	*pxTopOfStack = 0x10;
	/* RSP - task's stack pointer after iretq */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pxTaskStackTop;
	/* RFLAGS */
	pxTopOfStack--;
	*pxTopOfStack = portINITIAL_RFLAGS;
	/* CS */
	pxTopOfStack--;
	*pxTopOfStack = 0x08;
	/* RIP - task entry point */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pxCode;

	/* --- Context frame (19 items, 152 bytes) --- */
	/* r15 */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* r14 */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* r13 */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* r12 */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* r11 */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* r10 */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* r9 */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* r8 */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* rdi - first argument = pvParameters */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pvParameters;
	/* rsi */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* rbp */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* rbx */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* rdx */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* rcx */
	pxTopOfStack--;
	*pxTopOfStack = 0;
	/* rax */
	pxTopOfStack--;
	*pxTopOfStack = 0;

	/* rip - copy of task entry (written back to interrupt frame on restore) */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pxCode;

	/* rflags - copy (written back to interrupt frame on restore) */
	pxTopOfStack--;
	*pxTopOfStack = portINITIAL_RFLAGS;

	/* Critical nesting = 0 */
	pxTopOfStack--;
	*pxTopOfStack = portNO_CRITICAL_NESTING;

	/* FPU context = 0 */
	pxTopOfStack--;
	*pxTopOfStack = portNO_FLOATING_POINT_CONTEXT;

	return pxTopOfStack;
}

BaseType_t xPortStartScheduler( void )
{
	portDISABLE_INTERRUPTS();
	configSETUP_TICK_INTERRUPT();
	vPortRestoreTaskContext();
	return 0;
}

void vPortEndScheduler( void )
{
	configASSERT( ullCriticalNesting == 1000UL );
}

void vPortEnterCritical( void )
{
	portDISABLE_INTERRUPTS();
	ullCriticalNesting++;
	if( ullCriticalNesting == 1UL )
	{
		configASSERT( ullPortInterruptNesting == 0 );
	}
}

void vPortExitCritical( void )
{
	if( ullCriticalNesting > portNO_CRITICAL_NESTING )
	{
		ullCriticalNesting--;
		if( ullCriticalNesting == portNO_CRITICAL_NESTING )
		{
			portENABLE_INTERRUPTS();
		}
	}
}

void FreeRTOS_Tick_Handler( void )
{
	portDISABLE_INTERRUPTS();
	configCLEAR_TICK_INTERRUPT();
	portENABLE_INTERRUPTS();

	if( xTaskIncrementTick() != pdFALSE )
	{
		ullPortYieldRequired = pdTRUE;
	}
}

void vPortTaskUsesFPU( void )
{
	ullPortTaskHasFPUContext = pdTRUE;
}

void vPortClearInterruptMask( UBaseType_t uxNewMaskValue )
{
	if( uxNewMaskValue == pdFALSE )
	{
		portENABLE_INTERRUPTS();
	}
}

UBaseType_t uxPortSetInterruptMask( void )
{
	portDISABLE_INTERRUPTS();
	return pdTRUE;
}

void vPortValidateInterruptPriority( void )
{
	/* No-op on x86 (no priority-based interrupt controller in this port) */
}
