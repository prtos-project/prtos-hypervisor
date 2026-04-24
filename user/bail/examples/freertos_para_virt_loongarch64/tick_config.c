/*
 * tick_config.c - FreeRTOS tick timer using PRTOS hypercalls for LoongArch64
 *
 * Hypercall numbers (from core/include/loongarch64/hypercalls.h):
 *   get_time_nr      = 9
 *   set_timer_nr     = 10
 *   clear_irqmask_nr = 15
 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

/* PRTOS hypercall numbers */
#define PRTOS_GET_TIME_NR         9
#define PRTOS_SET_TIMER_NR        10
#define PRTOS_CLEAR_IRQMASK_NR   15
#define PRTOS_HW_CLOCK            0

static long prtos_hypercall2(unsigned long nr, unsigned long a1, unsigned long a2)
{
	register unsigned long r_a0 __asm__("$a0") = nr;
	register unsigned long r_a1 __asm__("$a1") = a1;
	register unsigned long r_a2 __asm__("$a2") = a2;
	register unsigned long r_a7 __asm__("$a7") = 0;
	__asm__ volatile("syscall 0" : "+r"(r_a0) : "r"(r_a1), "r"(r_a2), "r"(r_a7) : "memory");
	return (long)r_a0;
}

static long prtos_hypercall3(unsigned long nr, unsigned long a1, unsigned long a2, unsigned long a3)
{
	register unsigned long r_a0 __asm__("$a0") = nr;
	register unsigned long r_a1 __asm__("$a1") = a1;
	register unsigned long r_a2 __asm__("$a2") = a2;
	register unsigned long r_a3 __asm__("$a3") = a3;
	register unsigned long r_a7 __asm__("$a7") = 0;
	__asm__ volatile("syscall 0" : "+r"(r_a0) : "r"(r_a1), "r"(r_a2), "r"(r_a3), "r"(r_a7) : "memory");
	return (long)r_a0;
}

static unsigned long tick_period_usec;

void vConfigureTickInterrupt(void)
{
	unsigned long hw_clock = 0;
	prtos_hypercall2(PRTOS_GET_TIME_NR, PRTOS_HW_CLOCK, (unsigned long)&hw_clock);
	tick_period_usec = 1000000UL / configTICK_RATE_HZ;
	prtos_hypercall3(PRTOS_SET_TIMER_NR, PRTOS_HW_CLOCK, hw_clock + tick_period_usec, tick_period_usec);
	/* IRQ unmasking is done by vPortRestoreTaskContext (portASM.S)
	 * AFTER the first task's context is loaded, to avoid corrupting
	 * the scheduler stack with ISR SAVE_CONTEXT. */
}

void vClearTickInterrupt(void)
{
	/* Hypervisor auto-rearms the periodic ktimer.
	 * No guest-side re-arm needed. */
}
