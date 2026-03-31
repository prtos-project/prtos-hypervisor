/*
 * port.c for x86-64 FreeRTOS PRTOS para-virtualized partition
 *
 * Same as native port.c but uses GUEST_CS_SEL/GUEST_DS_SEL instead of
 * 0x08/0x10 for the fake interrupt frame, since the hypervisor expects
 * the partition's hardware GDT selectors in the iret frame.
 */
#include "FreeRTOS.h"
#include "task.h"

#define portINITIAL_RFLAGS			( ( StackType_t ) 0x0202 )
#define portNO_CRITICAL_NESTING		( ( unsigned long ) 0 )
#define portNO_FLOATING_POINT_CONTEXT	( ( unsigned long ) 0 )

/* Segment selectors: PRTOS GUEST_CS_SEL and GUEST_DS_SEL.
 * These match the hypervisor's GDT entries for partition code/data.
 * CONFIG_PARTITION_NO_GDT_ENTRIES = 32 by default. */
#ifndef PRTOS_GUEST_CS_SEL
#define PRTOS_GUEST_CS_SEL 0x13b
#endif
#ifndef PRTOS_GUEST_DS_SEL
#define PRTOS_GUEST_DS_SEL 0x143
#endif

unsigned long ullCriticalNesting = portNO_CRITICAL_NESTING;
unsigned long ullPortYieldRequired = pdFALSE;
unsigned long ullPortInterruptNesting = 0;
unsigned long ullPortTaskHasFPUContext = portNO_FLOATING_POINT_CONTEXT;

/* Dummy GIC addresses for link compatibility */
__attribute__(( used )) const unsigned long ullICCEOIR = 0;
__attribute__(( used )) const unsigned long ullICCIAR = 0;
__attribute__(( used )) const unsigned long ullICCPMR = 0;
__attribute__(( used )) const unsigned long ullMaxAPIPriorityMask = 0;

/*
 * Stack layout (growing downward):
 *   ------ Interrupt frame (faked for initial task) ------
 *   SS               (GUEST_DS_SEL)
 *   RSP              (task's real stack pointer)
 *   RFLAGS           (initial = 0x202, IF=1)
 *   CS               (GUEST_CS_SEL)
 *   RIP              (task entry point)
 *   ------ Context frame (portCONTEXT_SIZE = 152 bytes, 19 quadwords) ------
 *   r15..rax (15 registers)
 *   rip (copy)
 *   rflags (copy)
 *   ullCriticalNesting
 *   ullPortTaskHasFPUContext
 */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters )
{
	StackType_t *pxTaskStackTop = pxTopOfStack - 1;

	/* --- Fake interrupt frame (5 items, 40 bytes) --- */
	/* SS */
	pxTopOfStack--;
	*pxTopOfStack = PRTOS_GUEST_DS_SEL;
	/* RSP */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pxTaskStackTop;
	/* RFLAGS */
	pxTopOfStack--;
	*pxTopOfStack = portINITIAL_RFLAGS;
	/* CS */
	pxTopOfStack--;
	*pxTopOfStack = PRTOS_GUEST_CS_SEL;
	/* RIP */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pxCode;

	/* --- Context frame (19 items, 152 bytes) --- */
	pxTopOfStack--;		/* r15 */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* r14 */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* r13 */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* r12 */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* r11 */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* r10 */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* r9 */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* r8 */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* rdi = pvParameters (first arg) */
	*pxTopOfStack = ( StackType_t ) pvParameters;
	pxTopOfStack--;		/* rsi */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* rbp */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* rbx */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* rdx */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* rcx */
	*pxTopOfStack = 0;
	pxTopOfStack--;		/* rax */
	*pxTopOfStack = 0;

	/* rip copy */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pxCode;
	/* rflags copy */
	pxTopOfStack--;
	*pxTopOfStack = portINITIAL_RFLAGS;
	/* Critical nesting */
	pxTopOfStack--;
	*pxTopOfStack = portNO_CRITICAL_NESTING;
	/* FPU context */
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
	/* Interrupts are already disabled by the ISR entry code.
	 * Do NOT call portDISABLE/ENABLE_INTERRUPTS here to avoid
	 * re-enabling interrupts during ISR and triggering nested interrupts
	 * or critical section assertions. */
	configCLEAR_TICK_INTERRUPT();

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
