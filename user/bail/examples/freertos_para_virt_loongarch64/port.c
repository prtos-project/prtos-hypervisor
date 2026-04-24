/*
 * port.c - FreeRTOS port for LoongArch 64-bit para-virtualization on PRTOS
 */
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

#ifndef configSETUP_TICK_INTERRUPT
	#error configSETUP_TICK_INTERRUPT() must be defined.
#endif

#define portNO_CRITICAL_NESTING		( ( size_t ) 0 )
#define portNO_FLOATING_POINT_CONTEXT	( ( StackType_t ) 0 )

/* Initial CRMD: PLV=3, IE=1 (para-virt guest mode) */
#define portINITIAL_CRMD  0x7  /* PLV=3 (bits 1:0=11), IE=1 (bit 2) */

volatile unsigned long ullCriticalNesting = 9999UL;
unsigned long ullPortTaskHasFPUContext = pdFALSE;
unsigned long ullPortYieldRequired = pdFALSE;
unsigned long ullPortInterruptNesting = 0;

__attribute__(( used )) const unsigned long ullICCEOIR = 0;
__attribute__(( used )) const unsigned long ullICCIAR = 0;
__attribute__(( used )) const unsigned long ullICCPMR = 0;
__attribute__(( used )) const unsigned long ullMaxAPIPriorityMask = 0;
volatile unsigned int loongarch_dummy_pmr = 0xFF;
volatile unsigned int loongarch_dummy_bpr = 0;
volatile unsigned int loongarch_dummy_rpr = 0xFF;

extern void vPortRestoreTaskContext( void );

/*
 * Full-context frame (32 slots x 8 = 256 bytes).
 * Layout matches portASM.S SAVE_CONTEXT / _do_restore_regs.
 * See portASM.S for offset definitions.
 */
#define portFRAME_SLOTS   32
#define portFRAME_SIZE    (portFRAME_SLOTS * sizeof(StackType_t))

StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters )
{
	pxTopOfStack -= portFRAME_SLOTS;

	memset(pxTopOfStack, 0, portFRAME_SIZE);

	pxTopOfStack[0] = 0;                              /* ullCriticalNesting */
	pxTopOfStack[1] = ( StackType_t ) pxCode;         /* ra ($r1) = task entry */
	pxTopOfStack[3] = ( StackType_t ) pvParameters;   /* a0 ($r4) = pvParameters */

	return pxTopOfStack;
}

BaseType_t xPortStartScheduler( void )
{
	configSETUP_TICK_INTERRUPT();

	/* Start the first task via vPortRestoreTaskContext (in portASM.S).
	 * It loads context from pxCurrentTCB, enables IRQ delivery, and
	 * starts the task via IRET.  IRQ unmasking is done there to avoid
	 * ISR firing before the first task's stack is active. */
	vPortRestoreTaskContext();

	/* Should not reach here */
	return 0;
}

void vPortEndScheduler( void )
{
}

void vPortEnterCritical( void )
{
	portDISABLE_INTERRUPTS();
	ullCriticalNesting++;
	__asm volatile( "dbar 0" );
}

void vPortExitCritical( void )
{
	ullCriticalNesting--;
	if( ullCriticalNesting == portNO_CRITICAL_NESTING )
	{
		portENABLE_INTERRUPTS();
	}
}

UBaseType_t uxPortSetInterruptMask( void )
{
	/* In para-virt mode, use hypercall to mask interrupts */
	portDISABLE_INTERRUPTS();
	return 0;
}

void vPortClearInterruptMask( UBaseType_t uxNewMaskValue )
{
	(void)uxNewMaskValue;
	portENABLE_INTERRUPTS();
}

void vPortTaskUsesFPU( void )
{
	ullPortTaskHasFPUContext = pdTRUE;
}

volatile unsigned long g_isr_run_count = 0;

void FreeRTOS_Tick_Handler( void )
{
	extern unsigned long g_tick_count;
	g_isr_run_count++;
	g_tick_count++;
	configCLEAR_TICK_INTERRUPT();
	if( xTaskIncrementTick() != pdFALSE )
	{
		ullPortYieldRequired = pdTRUE;
	}
}

/* Simple C function called from ISR to test C calling from assembly ISR */
extern unsigned long g_tick_count;
void simple_tick_from_c(void)
{
	g_tick_count++;
}

/* Debug: called from vPortYield assembly after vTaskSwitchContext */
void debug_yield_info(void)
{
	extern void *pxCurrentTCB;
	extern void uart_puts(const char *);
	extern void uart_puthex(unsigned long);
	unsigned long *tcb = (unsigned long *)pxCurrentTCB;
	unsigned long sp_val = tcb[0];
	uart_puts("Y:");
	uart_puthex((unsigned long)pxCurrentTCB);
	uart_puts(" sp=");
	uart_puthex(sp_val);
	uart_puts("\n");
}

void vPortInstallFreeRTOSVectorTable( void )
{
	/* No-op for para-virt: trap entry is registered via PCT */
}

void vPortValidateInterruptPriority( void )
{
	/* No-op for para-virt */
}
