/*
 * FreeRTOS Kernel V10.0.1
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "cpu.h"
#include "uart.h"

static uint32_t cntfrq;     /* System frequency */

/* Timer used to generate the tick interrupt. */
void vConfigureTickInterrupt( void )
{
	// Disable the timer
	disable_cntv();
	// Get system frequency
	cntfrq = raw_read_cntfrq_el0();
	// Set tick rate
	raw_write_cntv_tval_el0(cntfrq/configTICK_RATE_HZ);
	// Enable the timer
	enable_cntv();
}
/*-----------------------------------------------------------*/

void vClearTickInterrupt( void )
{
	raw_write_cntv_tval_el0(cntfrq/configTICK_RATE_HZ);
}
/*-----------------------------------------------------------*/

void vApplicationIRQHandler( uint32_t ulICCIAR )
{
	uint32_t ulInterruptID;

	/* Interrupts cannot be re-enabled until the source of the interrupt is
	cleared. The ID of the interrupt is obtained by bitwise ANDing the ICCIAR
	value with 0x3FF. */
	ulInterruptID = ulICCIAR & 0x3FFUL;

	/* call handler function */
	if( ulInterruptID == TIMER_IRQ) {
		/* Generic Timer */
		FreeRTOS_Tick_Handler();
	}else{
		printf("\n%s(): IRQ happend (%u)\n", __func__, ulInterruptID);
	}
}
