/*
 * FreeRTOS port.c for AArch64 on PRTOS (HW-Virt)
 *
 * Based on FreeRTOS Kernel V10.0.1 GCC/ARM_CA57_64_BIT port.
 * Modified to use GICv3 ICC_* system registers instead of MMIO.
 */

#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"

#ifndef configUNIQUE_INTERRUPT_PRIORITIES
	#error configUNIQUE_INTERRUPT_PRIORITIES must be defined.
#endif

#ifndef configSETUP_TICK_INTERRUPT
	#error configSETUP_TICK_INTERRUPT() must be defined.
#endif

#ifndef configMAX_API_CALL_INTERRUPT_PRIORITY
	#error configMAX_API_CALL_INTERRUPT_PRIORITY must be defined.
#endif

#if configMAX_API_CALL_INTERRUPT_PRIORITY == 0
	#error configMAX_API_CALL_INTERRUPT_PRIORITY must not be set to 0
#endif

#if configMAX_API_CALL_INTERRUPT_PRIORITY > configUNIQUE_INTERRUPT_PRIORITIES
	#error configMAX_API_CALL_INTERRUPT_PRIORITY must be <= configUNIQUE_INTERRUPT_PRIORITIES
#endif

#if configUSE_PORT_OPTIMISED_TASK_SELECTION == 1
	#if( configMAX_PRIORITIES > 32 )
		#error configUSE_PORT_OPTIMISED_TASK_SELECTION requires configMAX_PRIORITIES <= 32
	#endif
#endif

#if configMAX_API_CALL_INTERRUPT_PRIORITY <= ( configUNIQUE_INTERRUPT_PRIORITIES / 2 )
	#error configMAX_API_CALL_INTERRUPT_PRIORITY must be > ( configUNIQUE_INTERRUPT_PRIORITIES / 2 )
#endif

#ifndef configCLEAR_TICK_INTERRUPT
	#define configCLEAR_TICK_INTERRUPT()
#endif

#define portNO_CRITICAL_NESTING			( ( size_t ) 0 )
#define portUNMASK_VALUE				( 0xFFUL )
#define portNO_FLOATING_POINT_CONTEXT	( ( StackType_t ) 0 )

#define portSP_ELx						( ( StackType_t ) 0x01 )
#define portSP_EL0						( ( StackType_t ) 0x00 )
#define portEL1							( ( StackType_t ) 0x04 )
#define portINITIAL_PSTATE				( portEL1 | portSP_EL0 )

#define portBINARY_POINT_BITS			( ( uint8_t ) 0x03 )
#define portAPSR_MODE_BITS_MASK			( 0x0C )
#define portDAIF_I						( 0x80 )

/* Unmask all interrupt priorities via system register. */
#define portCLEAR_INTERRUPT_MASK()									\
{																	\
	portDISABLE_INTERRUPTS();										\
	__port_icc_pmr_write( portUNMASK_VALUE );						\
	__asm volatile (	"DSB SY		\n"								\
						"ISB SY		\n" );							\
	portENABLE_INTERRUPTS();										\
}

#define portMAX_8_BIT_VALUE							( ( uint8_t ) 0xff )
#define portBIT_0_SET								( ( uint8_t ) 0x01 )

extern void vPortRestoreTaskContext( void );

/* Task context variables. */
volatile uint64_t ullCriticalNesting = 9999ULL;
uint64_t ullPortTaskHasFPUContext = pdFALSE;
uint64_t ullPortYieldRequired = pdFALSE;
uint64_t ullPortInterruptNesting = 0;

/* Used in portASM.S — system register access, no MMIO addresses needed.
 * These are kept as dummy values since portASM.S is rewritten to use
 * MRS/MSR directly. The constants are still referenced by the data
 * labels in portASM.S for backward compatibility of the symbol names. */
__attribute__(( used )) const uint64_t ullICCEOIR = 0;
__attribute__(( used )) const uint64_t ullICCIAR = 0;
__attribute__(( used )) const uint64_t ullICCPMR = 0;
__attribute__(( used )) const uint64_t ullMaxAPIPriorityMask = ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );

/*-----------------------------------------------------------*/

StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters )
{
	pxTopOfStack--;
	*pxTopOfStack = 0x0101010101010101ULL;	/* R1 */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pvParameters; /* R0 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0303030303030303ULL;	/* R3 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0202020202020202ULL;	/* R2 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0505050505050505ULL;	/* R5 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0404040404040404ULL;	/* R4 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0707070707070707ULL;	/* R7 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0606060606060606ULL;	/* R6 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0909090909090909ULL;	/* R9 */
	pxTopOfStack--;
	*pxTopOfStack = 0x0808080808080808ULL;	/* R8 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1111111111111111ULL;	/* R11 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1010101010101010ULL;	/* R10 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1313131313131313ULL;	/* R13 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1212121212121212ULL;	/* R12 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1515151515151515ULL;	/* R15 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1414141414141414ULL;	/* R14 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1717171717171717ULL;	/* R17 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1616161616161616ULL;	/* R16 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1919191919191919ULL;	/* R19 */
	pxTopOfStack--;
	*pxTopOfStack = 0x1818181818181818ULL;	/* R18 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2121212121212121ULL;	/* R21 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2020202020202020ULL;	/* R20 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2323232323232323ULL;	/* R23 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2222222222222222ULL;	/* R22 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2525252525252525ULL;	/* R25 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2424242424242424ULL;	/* R24 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2727272727272727ULL;	/* R27 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2626262626262626ULL;	/* R26 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2929292929292929ULL;	/* R29 */
	pxTopOfStack--;
	*pxTopOfStack = 0x2828282828282828ULL;	/* R28 */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) 0x00;	/* XZR */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) 0x00;	/* R30 */
	pxTopOfStack--;

	*pxTopOfStack = portINITIAL_PSTATE;
	pxTopOfStack--;

	*pxTopOfStack = ( StackType_t ) pxCode;
	pxTopOfStack--;

	*pxTopOfStack = portNO_CRITICAL_NESTING;
	pxTopOfStack--;

	*pxTopOfStack = portNO_FLOATING_POINT_CONTEXT;

	return pxTopOfStack;
}
/*-----------------------------------------------------------*/

BaseType_t xPortStartScheduler( void )
{
uint32_t ulAPSR;

	#if( configASSERT_DEFINED == 1 )
	{
		volatile uint8_t ucMaxPriorityValue;

		/* Determine priority bits by probing ICC_PMR_EL1. */
		__port_icc_pmr_write( portMAX_8_BIT_VALUE );
		__asm volatile("isb");
		ucMaxPriorityValue = (uint8_t)__port_icc_pmr_read();

		while( ( ucMaxPriorityValue & portBIT_0_SET ) != portBIT_0_SET )
		{
			ucMaxPriorityValue >>= ( uint8_t ) 0x01;
		}

		configASSERT( ucMaxPriorityValue >= portLOWEST_INTERRUPT_PRIORITY );

		/* Restore PMR */
		__port_icc_pmr_write( portUNMASK_VALUE );
	}
	#endif /* configASSERT_DEFINED */

	__asm volatile ( "MRS %0, CurrentEL" : "=r" ( ulAPSR ) );
	ulAPSR &= portAPSR_MODE_BITS_MASK;

	configASSERT( ulAPSR == portEL1 );
	if( ulAPSR == portEL1 )
	{
		/* Check binary point register. */
		configASSERT( ( __port_icc_bpr1_read() & portBINARY_POINT_BITS ) <= portMAX_BINARY_POINT_VALUE );

		if( ( __port_icc_bpr1_read() & portBINARY_POINT_BITS ) <= portMAX_BINARY_POINT_VALUE )
		{
			portDISABLE_INTERRUPTS();
			configSETUP_TICK_INTERRUPT();
			vPortRestoreTaskContext();
		}
	}

	return 0;
}
/*-----------------------------------------------------------*/

void vPortEndScheduler( void )
{
	configASSERT( ullCriticalNesting == 1000ULL );
}
/*-----------------------------------------------------------*/

void vPortEnterCritical( void )
{
	uxPortSetInterruptMask();

	ullCriticalNesting++;

	if( ullCriticalNesting == 1ULL )
	{
		configASSERT( ullPortInterruptNesting == 0 );
	}
}
/*-----------------------------------------------------------*/

void vPortExitCritical( void )
{
	if( ullCriticalNesting > portNO_CRITICAL_NESTING )
	{
		ullCriticalNesting--;

		if( ullCriticalNesting == portNO_CRITICAL_NESTING )
		{
			portCLEAR_INTERRUPT_MASK();
		}
	}
}
/*-----------------------------------------------------------*/

void FreeRTOS_Tick_Handler( void )
{
	#if( configASSERT_DEFINED == 1 )
	{
		uint32_t ulMaskBits;
		__asm volatile( "mrs %0, daif" : "=r"( ulMaskBits ) :: "memory" );
		configASSERT( ( ulMaskBits & portDAIF_I ) != 0 );
	}
	#endif

	/* Mask interrupts up to the max API call priority. */
	__port_icc_pmr_write( ( uint32_t )( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ) );
	__asm volatile (	"dsb sy		\n"
						"isb sy		\n" ::: "memory" );

	configCLEAR_TICK_INTERRUPT();
	portENABLE_INTERRUPTS();

	if( xTaskIncrementTick() != pdFALSE )
	{
		ullPortYieldRequired = pdTRUE;
	}

	portCLEAR_INTERRUPT_MASK();
}
/*-----------------------------------------------------------*/

void vPortTaskUsesFPU( void )
{
	ullPortTaskHasFPUContext = pdTRUE;
}
/*-----------------------------------------------------------*/

void vPortClearInterruptMask( UBaseType_t uxNewMaskValue )
{
	if( uxNewMaskValue == pdFALSE )
	{
		portCLEAR_INTERRUPT_MASK();
	}
}
/*-----------------------------------------------------------*/

UBaseType_t uxPortSetInterruptMask( void )
{
uint32_t ulReturn;

	portDISABLE_INTERRUPTS();
	if( __port_icc_pmr_read() == ( uint32_t ) ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ) )
	{
		ulReturn = pdTRUE;
	}
	else
	{
		ulReturn = pdFALSE;
		__port_icc_pmr_write( ( uint32_t ) ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ) );
		__asm volatile (	"dsb sy		\n"
							"isb sy		\n" ::: "memory" );
	}
	portENABLE_INTERRUPTS();

	return ulReturn;
}
/*-----------------------------------------------------------*/

#if( configASSERT_DEFINED == 1 )

	void vPortValidateInterruptPriority( void )
	{
		configASSERT( __port_icc_rpr_read() >= ( uint32_t ) ( configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT ) );
		configASSERT( ( __port_icc_bpr1_read() & portBINARY_POINT_BITS ) <= portMAX_BINARY_POINT_VALUE );
	}

#endif /* configASSERT_DEFINED */
