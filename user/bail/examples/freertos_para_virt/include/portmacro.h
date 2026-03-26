/*
 * FreeRTOS portmacro.h for AArch64 on PRTOS (HW-Virt)
 *
 * GIC access uses ICC_* system registers (virtual CPU interface)
 * instead of GICv2 MMIO.
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
	extern "C" {
#endif

/* Type definitions. */
#define portCHAR		char
#define portFLOAT		float
#define portDOUBLE		double
#define portLONG		long
#define portSHORT		short
#define portSTACK_TYPE	size_t
#define portBASE_TYPE	long

typedef portSTACK_TYPE StackType_t;
typedef portBASE_TYPE BaseType_t;
typedef uint64_t UBaseType_t;

typedef uint64_t TickType_t;
#define portMAX_DELAY ( ( TickType_t ) 0xffffffffffffffff )

#define portTICK_TYPE_IS_ATOMIC 1

/* Hardware specifics. */
#define portSTACK_GROWTH			( -1 )
#define portTICK_PERIOD_MS			( ( TickType_t ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT			16
#define portPOINTER_SIZE_TYPE 		uint64_t

/* Task utilities. */
#define portEND_SWITCHING_ISR( xSwitchRequired )\
{												\
extern uint64_t ullPortYieldRequired;			\
												\
	if( xSwitchRequired != pdFALSE )			\
	{											\
		ullPortYieldRequired = pdTRUE;			\
	}											\
}

#define portYIELD_FROM_ISR( x ) portEND_SWITCHING_ISR( x )
#define portYIELD() __asm volatile ( "SVC 0" ::: "memory" )

/* Critical section control */
extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
extern UBaseType_t uxPortSetInterruptMask( void );
extern void vPortClearInterruptMask( UBaseType_t uxNewMaskValue );
extern void vPortInstallFreeRTOSVectorTable( void );

#define portDISABLE_INTERRUPTS()								\
	__asm volatile ( "MSR DAIFSET, #2" ::: "memory" );			\
	__asm volatile ( "DSB SY" );								\
	__asm volatile ( "ISB SY" );

#define portENABLE_INTERRUPTS()									\
	__asm volatile ( "MSR DAIFCLR, #2" ::: "memory" );			\
	__asm volatile ( "DSB SY" );								\
	__asm volatile ( "ISB SY" );

#define portENTER_CRITICAL()		vPortEnterCritical();
#define portEXIT_CRITICAL()			vPortExitCritical();
#define portSET_INTERRUPT_MASK_FROM_ISR()		uxPortSetInterruptMask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)	vPortClearInterruptMask(x)

/* Task function macros */
#define portTASK_FUNCTION_PROTO( vFunction, pvParameters )	void vFunction( void *pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters )	void vFunction( void *pvParameters )

void FreeRTOS_Tick_Handler( void );

void vPortTaskUsesFPU( void );
#define portTASK_USES_FLOATING_POINT() vPortTaskUsesFPU()

#define portLOWEST_INTERRUPT_PRIORITY ( ( ( uint32_t ) configUNIQUE_INTERRUPT_PRIORITIES ) - 1UL )
#define portLOWEST_USABLE_INTERRUPT_PRIORITY ( portLOWEST_INTERRUPT_PRIORITY - 1UL )

/* Architecture specific optimisations. */
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

#define portNOP() __asm volatile( "NOP" )
#define portINLINE __inline

#ifdef __cplusplus
	} /* extern C */
#endif

/* Priority shift for interrupt priorities. */
#if configUNIQUE_INTERRUPT_PRIORITIES == 16
	#define portPRIORITY_SHIFT 4
	#define portMAX_BINARY_POINT_VALUE	3
#elif configUNIQUE_INTERRUPT_PRIORITIES == 32
	#define portPRIORITY_SHIFT 3
	#define portMAX_BINARY_POINT_VALUE	2
#elif configUNIQUE_INTERRUPT_PRIORITIES == 64
	#define portPRIORITY_SHIFT 2
	#define portMAX_BINARY_POINT_VALUE	1
#elif configUNIQUE_INTERRUPT_PRIORITIES == 128
	#define portPRIORITY_SHIFT 1
	#define portMAX_BINARY_POINT_VALUE	0
#elif configUNIQUE_INTERRUPT_PRIORITIES == 256
	#define portPRIORITY_SHIFT 0
	#define portMAX_BINARY_POINT_VALUE	0
#else
	#error Invalid configUNIQUE_INTERRUPT_PRIORITIES setting.
#endif

/*
 * GIC access via ICC_* system registers (virtual CPU interface).
 * No MMIO addresses needed.
 */
static inline uint32_t __port_icc_pmr_read(void) {
	uint64_t val;
	__asm volatile("mrs %0, S3_0_C4_C6_0" : "=r"(val));
	return (uint32_t)val;
}
static inline void __port_icc_pmr_write(uint32_t v) {
	uint64_t val = v;
	__asm volatile("msr S3_0_C4_C6_0, %0" :: "r"(val));
}
static inline uint32_t __port_icc_bpr1_read(void) {
	uint64_t val;
	__asm volatile("mrs %0, S3_0_C12_C12_3" : "=r"(val));
	return (uint32_t)val;
}
static inline uint32_t __port_icc_rpr_read(void) {
	uint64_t val;
	__asm volatile("mrs %0, S3_0_C12_C11_3" : "=r"(val));
	return (uint32_t)val;
}

#endif /* PORTMACRO_H */
