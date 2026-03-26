/*
 * FreeRTOS tick configuration for PRTOS hw-virt partition.
 *
 * Uses the ARM virtual timer (CNTV) accessible from EL1.
 * The hypervisor traps virtual timer interrupts (PPI 27) and
 * injects them via ICH_LR, masking CNTV_CTL.IMASK in the process.
 * vClearTickInterrupt() must clear IMASK to re-enable the timer.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "cpu.h"
#include "uart.h"

static uint32_t cntfrq;

void vConfigureTickInterrupt(void)
{
	/* Disable the virtual timer */
	disable_cntv();
	/* Get system counter frequency */
	cntfrq = raw_read_cntfrq_el0();
	/* Set tick interval */
	raw_write_cntv_tval_el0(cntfrq / configTICK_RATE_HZ);
	/* Enable the virtual timer (clear IMASK, set ENABLE) */
	uint32_t ctl = (1 << 0); /* ENABLE=1, IMASK=0 */
	__asm volatile("msr cntv_ctl_el0, %0" :: "r"((uint64_t)ctl));
	__asm volatile("isb");
}

void vClearTickInterrupt(void)
{
	/* Reload the timer for the next tick */
	raw_write_cntv_tval_el0(cntfrq / configTICK_RATE_HZ);
	/* Clear IMASK (bit 1) which the hypervisor sets when handling the
	 * virtual timer interrupt. Re-enable the timer output. */
	uint32_t ctl = (1 << 0); /* ENABLE=1, IMASK=0 */
	__asm volatile("msr cntv_ctl_el0, %0" :: "r"((uint64_t)ctl));
	__asm volatile("isb");
}

void vApplicationIRQHandler(uint32_t ulICCIAR)
{
	uint32_t ulInterruptID;

	ulInterruptID = ulICCIAR & 0x3FFUL;

	if (ulInterruptID == TIMER_IRQ) {
		FreeRTOS_Tick_Handler();
	} else {
		printf("\n%s(): IRQ happened (%u)\n", __func__, ulInterruptID);
	}
}
