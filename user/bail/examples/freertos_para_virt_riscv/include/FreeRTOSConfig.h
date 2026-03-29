/*
 * FreeRTOSConfig.h for RISC-V 64-bit para-virtualization on PRTOS
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "board.h"

#define configCPU_CLOCK_HZ						TIMER_FREQ
#define configUSE_PORT_OPTIMISED_TASK_SELECTION	1
#define configUSE_TICKLESS_IDLE					0
#define configTICK_RATE_HZ						( ( TickType_t ) 1000 )
#define configUSE_PREEMPTION					1
#define configUSE_IDLE_HOOK						0
#define configUSE_TICK_HOOK						0
#define configMAX_PRIORITIES					( 8 )
#define configMINIMAL_STACK_SIZE				( ( unsigned short ) 200 )
#define configTOTAL_HEAP_SIZE					( 124 * 1024 )
#define configMAX_TASK_NAME_LEN					( 10 )
#define configUSE_16_BIT_TICKS					0
#define configIDLE_SHOULD_YIELD					1
#define configUSE_MUTEXES						1
#define configQUEUE_REGISTRY_SIZE				8
#define configUSE_RECURSIVE_MUTEXES				1
#define configUSE_APPLICATION_TASK_TAG			0
#define configUSE_COUNTING_SEMAPHORES			1
#define configUSE_QUEUE_SETS					1
#define configSUPPORT_STATIC_ALLOCATION			0
#define configSUPPORT_DYNAMIC_ALLOCATION		1

#define configUSE_CO_ROUTINES 					0
#define configMAX_CO_ROUTINE_PRIORITIES 		( 2 )

#define configUSE_TIMERS						1
#define configTIMER_TASK_PRIORITY				( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH				5
#define configTIMER_TASK_STACK_DEPTH			( configMINIMAL_STACK_SIZE * 2 )

#define INCLUDE_vTaskPrioritySet				1
#define INCLUDE_uxTaskPriorityGet				1
#define INCLUDE_vTaskDelete						1
#define INCLUDE_vTaskCleanUpResources			1
#define INCLUDE_vTaskSuspend					1
#define INCLUDE_vTaskDelayUntil					1
#define INCLUDE_vTaskDelay						1
#define INCLUDE_xTimerPendFunctionCall			1
#define INCLUDE_eTaskGetState					1
#define INCLUDE_xTaskAbortDelay					1

#define configUSE_STATS_FORMATTING_FUNCTIONS	0
#define configGENERATE_RUN_TIME_STATS 			0
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#define portGET_RUN_TIME_COUNTER_VALUE()

#define configCOMMAND_INT_MAX_OUTPUT_SIZE 		2096

void vMainAssertCalled( const char *pcFileName, unsigned int ulLineNumber );
#define configASSERT( x ) if( ( x ) == 0 ) { vMainAssertCalled( __FILE__, __LINE__ ); }

#define configTASK_RETURN_ADDRESS	((void *)0)

#define recmuCONTROLLING_TASK_PRIORITY ( configMAX_PRIORITIES - 2 )

void vConfigureTickInterrupt( void );
#define configSETUP_TICK_INTERRUPT() vConfigureTickInterrupt()

void vClearTickInterrupt( void );
#define configCLEAR_TICK_INTERRUPT() vClearTickInterrupt()

/* Dummy GIC definitions for compatibility with FreeRTOS core code */
#define configUNIQUE_INTERRUPT_PRIORITIES		32
#define configMAX_API_CALL_INTERRUPT_PRIORITY	18
#define configINTERRUPT_CONTROLLER_BASE_ADDRESS	0

#define fabs( x ) __builtin_fabs( x )

#define configUSE_TRACE_FACILITY				0

#endif /* FREERTOS_CONFIG_H */
