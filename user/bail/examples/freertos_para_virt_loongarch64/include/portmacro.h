/*
 * portmacro.h for LoongArch 64-bit para-virtualization on PRTOS
 *
 * PRTOS hypercall numbers (from core/include/loongarch64/hypercalls.h):
 *   set_irqmask_nr  = 16  (mask/disable virtual IRQs)
 *   clear_irqmask_nr = 15  (unmask/enable virtual IRQs)
 *   write_object_nr  = 12  (write to console object)
 *   get_time_nr      = 9
 *   set_timer_nr     = 10
 *   NR_HYPERCALLS    = 44  (IRET)
 *   NR_HYPERCALLS+1  = 45  (RAISE_TRAP)
 */
#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

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

/* portYIELD: call vPortYield (assembly) which does full context
 * save → vTaskSwitchContext → context restore. */
extern void vPortYield( void );
#define portYIELD() vPortYield()

extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
extern UBaseType_t uxPortSetInterruptMask( void );
extern void vPortClearInterruptMask( UBaseType_t uxNewMaskValue );
extern void vPortInstallFreeRTOSVectorTable( void );

/* Para-virt: use PRTOS hypercalls to mask/unmask virtual IRQs.
 * set_irqmask_nr = 16: sets mask bits → blocks delivery
 * clear_irqmask_nr = 15: clears mask bits → allows delivery
 * Args: a1 = hw_irqs_mask, a2 = ext_irqs_mask.
 * a7 MUST be 0 to select the para-virt hypercall path. */
#define portDISABLE_INTERRUPTS()  do { \
	register unsigned long _a0 __asm__("$a0") = 16; \
	register unsigned long _a1 __asm__("$a1") = 0xFFFFFFFF; \
	register unsigned long _a2 __asm__("$a2") = 0xFFFFFFFF; \
	register unsigned long _a7 __asm__("$a7") = 0; \
	__asm__ volatile("syscall 0" : "+r"(_a0) : "r"(_a1), "r"(_a2), "r"(_a7) : "memory"); \
} while(0)

#define portENABLE_INTERRUPTS()  do { \
	register unsigned long _a0 __asm__("$a0") = 15; \
	register unsigned long _a1 __asm__("$a1") = 0xFFFFFFFF; \
	register unsigned long _a2 __asm__("$a2") = 0xFFFFFFFF; \
	register unsigned long _a7 __asm__("$a7") = 0; \
	__asm__ volatile("syscall 0" : "+r"(_a0) : "r"(_a1), "r"(_a2), "r"(_a7) : "memory"); \
} while(0)

#define portENTER_CRITICAL()		vPortEnterCritical();
#define portEXIT_CRITICAL()			vPortExitCritical();
#define portSET_INTERRUPT_MASK_FROM_ISR()		uxPortSetInterruptMask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)	vPortClearInterruptMask(x)

#define portTASK_FUNCTION_PROTO( vFunction, pvParameters ) void vFunction( void *pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters ) void vFunction( void *pvParameters )

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
	#define portASSERT_IF_INTERRUPT_PRIORITY_INVALID() vPortValidateInterruptPriority()
#endif

#define portNOP() __asm volatile( "nop" )
#define portINLINE __inline

#ifdef __cplusplus
}
#endif

/* Dummy GIC definitions (required by FreeRTOS port layer) */
#define portPRIORITY_SHIFT 3
#define portMAX_BINARY_POINT_VALUE 2

#define portICCPMR_PRIORITY_MASK_REGISTER          loongarch_dummy_pmr
#define portICCIAR_INTERRUPT_ACKNOWLEDGE_REGISTER_ADDRESS  0
#define portICCEOIR_END_OF_INTERRUPT_REGISTER_ADDRESS      0
#define portICCPMR_PRIORITY_MASK_REGISTER_ADDRESS          0
#define portICCBPR_BINARY_POINT_REGISTER           loongarch_dummy_bpr
#define portICCRPR_RUNNING_PRIORITY_REGISTER       loongarch_dummy_rpr

extern volatile unsigned int loongarch_dummy_pmr;
extern volatile unsigned int loongarch_dummy_bpr;
extern volatile unsigned int loongarch_dummy_rpr;

#endif /* PORTMACRO_H */
