/*
 * FreeRTOS tick configuration for x86-64 using PIT (8254)
 */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

/* I/O port access */
static inline void outb(unsigned short port, unsigned char val)
{
	__asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

void vConfigureTickInterrupt( void )
{
	unsigned int divisor = PIT_FREQUENCY / configTICK_RATE_HZ;

	/* Channel 0, access mode lobyte/hibyte, mode 2 (rate generator) */
	outb(PIT_CMD, 0x34);

	/* Set divisor low byte then high byte */
	outb(PIT_CH0_DATA, (unsigned char)(divisor & 0xFF));
	outb(PIT_CH0_DATA, (unsigned char)((divisor >> 8) & 0xFF));
}

void vClearTickInterrupt( void )
{
	/* PIT auto-reloads, nothing to clear */
}
