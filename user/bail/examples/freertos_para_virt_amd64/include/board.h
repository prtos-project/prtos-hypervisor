#ifndef _BOARD_H
#define _BOARD_H

/* Timer IRQ vector: PRTOS routes ext HW timer to vector 224 */
#define TIMER_IRQ_VECTOR   224

/* Yield vector: FreeRTOS uses int $3 (breakpoint). The hypervisor
 * routes this trap to the partition's IDT via fix_stack().
 * Vector 3 has DPL=3 in the hypervisor IDT, so ring 3 can use it. */
#define YIELD_VECTOR       3

#endif /* _BOARD_H */
