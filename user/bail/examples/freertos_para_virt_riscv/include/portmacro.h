/*
 * portmacro.h for RISC-V 64-bit para-virtualization on PRTOS
 *
 * Same as the native port but portYIELD uses RAISE_TRAP ecall
 * instead of sip.SSIP software interrupt.
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
	extern "C" {
#endif

/* Type definitions - same as native */
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

/* portYIELD: trigger a supervisor software interrupt to self.
 * Writing to sip.SSIP (bit 1) triggers the software interrupt.
 * In VS-mode this writes vsip.VSSIP which is delegated via hideleg. */
#define portYIELD()  __asm volatile (			\
	"csrs sip, %0" :: "r"(1UL << 1) : "memory" \
)

extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
extern UBaseType_t uxPortSetInterruptMask( void );
extern void vPortClearInterruptMask( UBaseType_t uxNewMaskValue );
extern void vPortInstallFreeRTOSVectorTable( void );

#define portDISABLE_INTERRUPTS()										\
	__asm volatile ( "csrc sstatus, %0" :: "r"(1UL << 1) : "memory" );	\
	__asm volatile ( "fence" );

#define portENABLE_INTERRUPTS()											\
	__asm volatile ( "csrs sstatus, %0" :: "r"(1UL << 1) : "memory" );	\
	__asm volatile ( "fence" );

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
	#define portGET_HIGHEST_PRIORITY( uxTopPriority, uxReadyPriorities ) uxTopPriority = ( 31 - __builtin_clz( uxReadyPriorities ) )
#endif

#ifdef configASSERT
	void vPortValidateInterruptPriority( void );
	#define portASSERT_IF_INTERRUPT_PRIORITY_INVALID() 	vPortValidateInterruptPriority()
#endif

#define portNOP() __asm volatile( "nop" )
#define portINLINE __inline

#ifdef __cplusplus
	}
#endif

/* Dummy GIC definitions */
#define portPRIORITY_SHIFT 3
#define portMAX_BINARY_POINT_VALUE 2

#define portICCPMR_PRIORITY_MASK_REGISTER          riscv_dummy_pmr
#define portICCIAR_INTERRUPT_ACKNOWLEDGE_REGISTER_ADDRESS  0
#define portICCEOIR_END_OF_INTERRUPT_REGISTER_ADDRESS      0
#define portICCPMR_PRIORITY_MASK_REGISTER_ADDRESS          0
#define portICCBPR_BINARY_POINT_REGISTER           riscv_dummy_bpr
#define portICCRPR_RUNNING_PRIORITY_REGISTER       riscv_dummy_rpr

extern volatile unsigned int riscv_dummy_pmr;
extern volatile unsigned int riscv_dummy_bpr;
extern volatile unsigned int riscv_dummy_rpr;

#endif /* PORTMACRO_H */
