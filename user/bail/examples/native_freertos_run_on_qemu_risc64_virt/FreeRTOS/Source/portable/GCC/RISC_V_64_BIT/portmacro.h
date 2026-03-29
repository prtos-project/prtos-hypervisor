/*
 * FreeRTOS Kernel V10.0.1
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * portmacro.h for RISC-V 64-bit (S-mode / VS-mode)
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
	extern "C" {
#endif

/*-----------------------------------------------------------
 * Port specific definitions.
 *-----------------------------------------------------------*/

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
typedef unsigned long UBaseType_t;

typedef unsigned long TickType_t;
#define portMAX_DELAY ( ( TickType_t ) 0xffffffffffffffffUL )

/* 64-bit tick type on a 64-bit architecture, so reads of the tick count do
not need to be guarded with a critical section. */
#define portTICK_TYPE_IS_ATOMIC 1

/*-----------------------------------------------------------*/

/* Hardware specifics. */
#define portSTACK_GROWTH			( -1 )
#define portTICK_PERIOD_MS			( ( TickType_t ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT			16
#define portPOINTER_SIZE_TYPE 		unsigned long

/*-----------------------------------------------------------*/

/* Task utilities. */

/* Called at the end of an ISR that can cause a context switch. */
#define portEND_SWITCHING_ISR( xSwitchRequired )	\
{													\
extern unsigned long ullPortYieldRequired;			\
													\
	if( xSwitchRequired != pdFALSE )				\
	{												\
		ullPortYieldRequired = pdTRUE;				\
	}												\
}

#define portYIELD_FROM_ISR( x ) portEND_SWITCHING_ISR( x )

/* portYIELD: trigger a supervisor software interrupt to self.
 * Writing to sip.SSIP (bit 1) triggers the software interrupt.
 * In VS-mode this writes vsip.SSIP which also works. */
#define portYIELD()  __asm volatile (			\
	"csrs sip, %0" :: "r"(1UL << 1) : "memory" \
)

/*-----------------------------------------------------------
 * Critical section control
 *----------------------------------------------------------*/

extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
extern UBaseType_t uxPortSetInterruptMask( void );
extern void vPortClearInterruptMask( UBaseType_t uxNewMaskValue );
extern void vPortInstallFreeRTOSVectorTable( void );

/* Disable/enable interrupts via sstatus.SIE (bit 1).
 * fence instructions ensure memory ordering. */
#define portDISABLE_INTERRUPTS()										\
	__asm volatile ( "csrc sstatus, %0" :: "r"(1UL << 1) : "memory" );	\
	__asm volatile ( "fence" );

#define portENABLE_INTERRUPTS()											\
	__asm volatile ( "csrs sstatus, %0" :: "r"(1UL << 1) : "memory" );	\
	__asm volatile ( "fence" );


/* Critical section macros. RISC-V does not have priority masking like ARM GIC,
 * so critical sections simply disable/enable global interrupts. */
#define portENTER_CRITICAL()		vPortEnterCritical();
#define portEXIT_CRITICAL()			vPortExitCritical();
#define portSET_INTERRUPT_MASK_FROM_ISR()		uxPortSetInterruptMask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)	vPortClearInterruptMask(x)

/*-----------------------------------------------------------*/

/* Task function macros. */
#define portTASK_FUNCTION_PROTO( vFunction, pvParameters )	void vFunction( void *pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters )	void vFunction( void *pvParameters )

/* Prototype of the FreeRTOS tick handler. */
void FreeRTOS_Tick_Handler( void );

/* Any task that uses the floating point unit MUST call vPortTaskUsesFPU()
before any floating point instructions are executed. */
void vPortTaskUsesFPU( void );
#define portTASK_USES_FLOATING_POINT() vPortTaskUsesFPU()

/* Dummy definitions to satisfy FreeRTOS core code that references GIC priority
 * concepts. On RISC-V these are not used. */
#define portLOWEST_INTERRUPT_PRIORITY ( ( ( unsigned int ) configUNIQUE_INTERRUPT_PRIORITIES ) - 1UL )
#define portLOWEST_USABLE_INTERRUPT_PRIORITY ( portLOWEST_INTERRUPT_PRIORITY - 1UL )

/* Architecture specific optimisations. */
#ifndef configUSE_PORT_OPTIMISED_TASK_SELECTION
	#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#endif

#if configUSE_PORT_OPTIMISED_TASK_SELECTION == 1

	/* Store/clear the ready priorities in a bit map. */
	#define portRECORD_READY_PRIORITY( uxPriority, uxReadyPriorities ) ( uxReadyPriorities ) |= ( 1UL << ( uxPriority ) )
	#define portRESET_READY_PRIORITY( uxPriority, uxReadyPriorities ) ( uxReadyPriorities ) &= ~( 1UL << ( uxPriority ) )

	#define portGET_HIGHEST_PRIORITY( uxTopPriority, uxReadyPriorities ) uxTopPriority = ( 31 - __builtin_clz( uxReadyPriorities ) )

#endif /* configUSE_PORT_OPTIMISED_TASK_SELECTION */

#ifdef configASSERT
	void vPortValidateInterruptPriority( void );
	#define portASSERT_IF_INTERRUPT_PRIORITY_INVALID() 	vPortValidateInterruptPriority()
#endif /* configASSERT */

#define portNOP() __asm volatile( "nop" )
#define portINLINE __inline

#ifdef __cplusplus
	} /* extern C */
#endif

/* RISC-V does not use GIC priority shifting. Define dummy values so the
 * port.c code (which references these in the original ARM port) can compile.
 * In practice, the RISC-V port.c does not use these. */
#define portPRIORITY_SHIFT 3
#define portMAX_BINARY_POINT_VALUE 2

/* RISC-V has no GIC MMIO registers. Define dummy addresses so the portmacro
 * stays compatible with FreeRTOS core expectations. port.c overrides these. */
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
