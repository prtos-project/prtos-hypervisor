/*
 * FreeRTOS tick configuration for PRTOS para-virt on amd64
 * Uses prtos_set_timer() hypercall for periodic timer.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

/* PRTOS timer types */
#define PRTOS_HW_CLOCK  0

/* PRTOS time is in microseconds */
#define USECS_PER_SEC   1000000ULL
#define TICK_INTERVAL_US (USECS_PER_SEC / configTICK_RATE_HZ)

/* PRTOS hypercall */
extern int prtos_set_timer(unsigned int clock_id, long long abs_time, long long interval);
extern int prtos_get_time(unsigned int clock_id, long long *time);

void vConfigureTickInterrupt(void)
{
	long long now = 0;
	prtos_get_time(PRTOS_HW_CLOCK, &now);
	prtos_set_timer(PRTOS_HW_CLOCK, now + TICK_INTERVAL_US, TICK_INTERVAL_US);
}

void vClearTickInterrupt(void)
{
	/* PRTOS periodic timer auto-repeats, nothing to clear */
}
