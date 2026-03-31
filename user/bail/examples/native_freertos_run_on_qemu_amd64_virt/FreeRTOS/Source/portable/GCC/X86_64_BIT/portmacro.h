/*
 * portmacro.h for x86-64 (native, bare-metal)
 *
 * FreeRTOS port for 64-bit x86 using cli/sti for interrupt masking
 * and the PIT timer for tick generation.
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
	extern "C" {
#endif

/* Type definitions */
#define portCHAR		char
#define portFLOAT		float
#define portDOUBLE		double
#define portLONG		long
#define portSHORT		short
#define portSTACK_TYPE	size_t
#define portBASE_TYPE	long

typedef portSTACK_TYPE StackType_t;
typedef portBASE_TYPE BaseType_t;
typedef unsigned long UBaseType_t;

typedef unsigned long TickType_t;
#define portMAX_DELAY ( ( TickType_t ) 0xffffffffffffffffUL )

#define portTICK_TYPE_IS_ATOMIC 1

#define portSTACK_GROWTH			( -1 )
#define portTICK_PERIOD_MS			( ( TickType_t ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT			16
#define portPOINTER_SIZE_TYPE 		unsigned long

#define portEND_SWITCHING_ISR( xSwitchRequired )	\
{													\
extern unsigned long ullPortYieldRequired;			\
	if( xSwitchRequired != pdFALSE )				\
	{												\
		ullPortYieldRequired = pdTRUE;				\
	}												\
}

#define portYIELD_FROM_ISR( x ) portEND_SWITCHING_ISR( x )

/* portYIELD: trigger a software interrupt via int $0x80 */
#define portYIELD()  __asm volatile ( "int $0x80" ::: "memory" )

extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
extern UBaseType_t uxPortSetInterruptMask( void );
extern void vPortClearInterruptMask( UBaseType_t uxNewMaskValue );

/* On x86-64 native, use cli/sti for interrupt control */
#define portDISABLE_INTERRUPTS()	__asm volatile ( "cli" ::: "memory" )
#define portENABLE_INTERRUPTS()		__asm volatile ( "sti" ::: "memory" )

#define portENTER_CRITICAL()		vPortEnterCritical();
#define portEXIT_CRITICAL()			vPortExitCritical();
#define portSET_INTERRUPT_MASK_FROM_ISR()		uxPortSetInterruptMask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)	vPortClearInterruptMask(x)

#define portTASK_FUNCTION_PROTO( vFunction, pvParameters )	void vFunction( void *pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters )	void vFunction( void *pvParameters )

void FreeRTOS_Tick_Handler( void );

void vPortTaskUsesFPU( void );
#define portTASK_USES_FLOATING_POINT() vPortTaskUsesFPU()

#define portLOWEST_INTERRUPT_PRIORITY ( ( ( unsigned int ) configUNIQUE_INTERRUPT_PRIORITIES ) - 1UL )
#define portLOWEST_USABLE_INTERRUPT_PRIORITY ( portLOWEST_INTERRUPT_PRIORITY - 1UL )

#ifndef configUSE_PORT_OPTIMISED_TASK_SELECTION
	#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#endif

#if configUSE_PORT_OPTIMISED_TASK_SELECTION == 1
	#define portRECORD_READY_PRIORITY( uxPriority, uxReadyPriorities ) ( uxReadyPriorities ) |= ( 1UL << ( uxPriority ) )
	#define portRESET_READY_PRIORITY( uxPriority, uxReadyPriorities ) ( uxReadyPriorities ) &= ~( 1UL << ( uxPriority ) )
	#define portGET_HIGHEST_PRIORITY( uxTopPriority, uxReadyPriorities ) uxTopPriority = ( 63 - __builtin_clzl( uxReadyPriorities ) )
#endif

#ifdef configASSERT
	void vPortValidateInterruptPriority( void );
	#define portASSERT_IF_INTERRUPT_PRIORITY_INVALID() 	vPortValidateInterruptPriority()
#endif

#define portNOP() __asm volatile( "nop" )
#define portINLINE __inline

/* Dummy GIC definitions for FreeRTOS core compatibility (x86 doesn't have GIC) */
#define portPRIORITY_SHIFT 3
#define portMAX_BINARY_POINT_VALUE 2
#define portICCPMR_PRIORITY_MASK_REGISTER          x86_dummy_pmr
#define portICCIAR_INTERRUPT_ACKNOWLEDGE_REGISTER_ADDRESS  0
#define portICCEOIR_END_OF_INTERRUPT_REGISTER_ADDRESS      0
#define portICCPMR_PRIORITY_MASK_REGISTER_ADDRESS          0
#define portICCBPR_BINARY_POINT_REGISTER           x86_dummy_bpr
#define portICCRPR_RUNNING_PRIORITY_REGISTER       x86_dummy_rpr

extern volatile unsigned int x86_dummy_pmr;
extern volatile unsigned int x86_dummy_bpr;

#ifdef __cplusplus
	}
#endif

#endif /* PORTMACRO_H */
