/*
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * port.c for LoongArch64 (PLV0 / Guest mode via LVZ)
 */

/* Standard includes. */
#include <stdlib.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "portmacro.h"

/* LoongArch64 CSR definitions */
#define CSR_CRMD    0x0
#define CSR_PRMD    0x1
#define CSR_ECFG    0x4
#define CSR_ESTAT   0x5
#define CSR_ERA     0x6
#define CSR_EENTRY  0xC

#define CRMD_IE     (1UL << 2)

/* Initial PRMD value: PIE=1 (bit 2), PPLV=0 (Ring 0) */
#define portINITIAL_PRMD  (1UL << 2)

/* Stack frame layout (must match portASM.S):
 *   Slot  0 (offset   0): ullPortTaskHasFPUContext
 *   Slot  1 (offset   8): ullCriticalNesting
 *   Slot  2 (offset  16): PRMD
 *   Slot  3 (offset  24): ERA
 *   Slot  4 (offset  32): ra  (r1)
 *   Slot  5 (offset  40): tp  (r2)
 *   Slot  6 (offset  48): a0  (r4)
 *   ...
 *   Slot 33 (offset 264): s8  (r31)
 */
#define portCONTEXT_SIZE  34

/* Critical nesting count */
volatile UBaseType_t ullCriticalNesting = 0xAAAAAAAAUL;

/* Used to track task switch requests */
volatile UBaseType_t ullPortYieldRequired = pdFALSE;

/* Interrupt nesting counter (used by portASM.S) */
volatile UBaseType_t ullPortInterruptNesting = 0;

/* FPU context flag (0 = no FPU, 1 = has FPU context) */
volatile UBaseType_t ullPortTaskHasFPUContext = 0;

/* Current running task TCB pointer, used by portASM.S */
extern volatile void * pxCurrentTCB;

/* Timer setup, called before scheduler starts */
extern void vConfigureTickInterrupt(void);

/* portASM.S provides the context restore */
extern void vPortRestoreTaskContext(void);

/*-----------------------------------------------------------*/

StackType_t * pxPortInitialiseStack(StackType_t * pxTopOfStack,
                                     TaskFunction_t pxCode,
                                     void * pvParameters)
{
    /* Allocate space for the context on the stack */
    pxTopOfStack -= portCONTEXT_SIZE;

    memset(pxTopOfStack, 0, portCONTEXT_SIZE * sizeof(StackType_t));

    /* Slot 0: ullPortTaskHasFPUContext = 0 (no FPU) */
    pxTopOfStack[0] = 0;

    /* Slot 1: ullCriticalNesting = 0 */
    pxTopOfStack[1] = 0;

    /* Slot 2: PRMD = PIE enabled (interrupts on after ertn) */
    pxTopOfStack[2] = portINITIAL_PRMD;

    /* Slot 3: ERA = task entry point */
    pxTopOfStack[3] = (StackType_t)pxCode;

    /* Slot 4: ra (r1) = task entry point (return address) */
    pxTopOfStack[4] = (StackType_t)pxCode;

    /* Slot 6: a0 (r4) = task parameter */
    pxTopOfStack[6] = (StackType_t)pvParameters;

    return pxTopOfStack;
}

/*-----------------------------------------------------------*/

BaseType_t xPortStartScheduler(void)
{
    /* Configure the timer for tick interrupt */
    vConfigureTickInterrupt();

    /* Restore the context of the first task */
    vPortRestoreTaskContext();

    /* Should not reach here */
    return pdFAIL;
}

/*-----------------------------------------------------------*/

void vPortEndScheduler(void)
{
    /* Not implemented */
}

/*-----------------------------------------------------------*/

void vPortEnterCritical(void)
{
    portDISABLE_INTERRUPTS();
    ullCriticalNesting++;
}

/*-----------------------------------------------------------*/

void vPortExitCritical(void)
{
    configASSERT(ullCriticalNesting > 0);
    ullCriticalNesting--;

    if (ullCriticalNesting == 0)
    {
        portENABLE_INTERRUPTS();
    }
}

/*-----------------------------------------------------------*/

/* ISR-safe interrupt mask: disable interrupts, return old CRMD */
UBaseType_t uxPortSetInterruptMask(void)
{
    UBaseType_t ulOldCRMD;
    __asm volatile ( "csrrd %0, 0x0" : "=r"(ulOldCRMD) );
    UBaseType_t ulNewCRMD = ulOldCRMD & ~CRMD_IE;
    __asm volatile ( "csrwr %0, 0x0" :: "r"(ulNewCRMD) );
    __asm volatile ( "dbar 0" ::: "memory" );
    return ulOldCRMD & CRMD_IE;
}

/*-----------------------------------------------------------*/

/* ISR-safe interrupt restore: restore IE bit from saved value */
void vPortClearInterruptMask(UBaseType_t uxSavedInterruptStatus)
{
    if (uxSavedInterruptStatus != 0)
    {
        portENABLE_INTERRUPTS();
    }
}

/*-----------------------------------------------------------*/

/* FreeRTOS tick handler */
void FreeRTOS_Tick_Handler(void)
{
    /* Increment the RTOS tick */
    if (xTaskIncrementTick() != pdFALSE)
    {
        ullPortYieldRequired = pdTRUE;
    }
}

/*-----------------------------------------------------------*/

/* Mark current task as using FPU */
void vPortTaskUsesFPU(void)
{
    ullPortTaskHasFPUContext = pdTRUE;
}

/*-----------------------------------------------------------*/

/* Validate interrupt priority - no-op for LoongArch64 (no priority levels) */
void vPortValidateInterruptPriority(void)
{
    /* LoongArch64 does not have interrupt priority levels like ARM GIC.
     * All interrupts are flat priority. This is a no-op. */
}
