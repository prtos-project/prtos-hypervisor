/*
 * tick_config.c - FreeRTOS tick timer using SBI set_timer for RISC-V
 *
 * Uses SBI legacy set_timer ecall to program the supervisor timer.
 * Timer interrupts arrive as supervisor timer interrupt (scause bit 63 | 5).
 */

#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

/* Tick interval in timer counts */
static unsigned long tick_interval;

/* SBI legacy set_timer call */
static void sbi_set_timer(unsigned long stime)
{
	register unsigned long a0 __asm__("a0") = stime;
	register unsigned long a7 __asm__("a7") = 0; /* SBI_SET_TIMER */
	__asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

/* Read RISC-V time CSR */
static inline unsigned long read_time(void)
{
	unsigned long val;
	__asm__ __volatile__("rdtime %0" : "=r"(val));
	return val;
}

void vConfigureTickInterrupt(void)
{
	tick_interval = TIMER_FREQ / configTICK_RATE_HZ;

	/* Enable supervisor timer interrupt (bit 5 of sie) */
	__asm__ __volatile__("csrs sie, %0" :: "r"(1UL << 5));

	/* Program first tick */
	sbi_set_timer(read_time() + tick_interval);
}

void vClearTickInterrupt(void)
{
	/* Reprogram timer for next tick */
	sbi_set_timer(read_time() + tick_interval);
}

/*
 * Called from the trap handler when a supervisor timer interrupt occurs.
 */
void vApplicationIRQHandler(unsigned long scause)
{
	unsigned long code = scause & 0x7FFFFFFFFFFFFFFFUL;

	if (code == 5) {
		/* Supervisor timer interrupt */
		FreeRTOS_Tick_Handler();
	}
}
