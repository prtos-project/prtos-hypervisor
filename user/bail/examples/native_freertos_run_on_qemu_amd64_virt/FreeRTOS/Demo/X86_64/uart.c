/*
 * UART driver for x86-64 (COM1 at 0x3F8) using I/O port instructions.
 */
#include <stdint.h>
#include "uart.h"
#include "board.h"

static inline void outb(unsigned short port, unsigned char val)
{
	__asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline unsigned char inb(unsigned short port)
{
	unsigned char ret;
	__asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
	return ret;
}

#define UART_THR  (UART_BASE + 0)  /* Transmit Holding Register */
#define UART_RBR  (UART_BASE + 0)  /* Receive Buffer Register */
#define UART_DLL  (UART_BASE + 0)  /* Divisor Latch Low */
#define UART_DLH  (UART_BASE + 1)  /* Divisor Latch High */
#define UART_IER  (UART_BASE + 1)  /* Interrupt Enable Register */
#define UART_FCR  (UART_BASE + 2)  /* FIFO Control Register */
#define UART_LCR  (UART_BASE + 3)  /* Line Control Register */
#define UART_MCR  (UART_BASE + 4)  /* Modem Control Register */
#define UART_LSR  (UART_BASE + 5)  /* Line Status Register */

void uart_init(void)
{
	/* Disable interrupts */
	outb(UART_IER, 0x00);

	/* Enable DLAB (set baud rate divisor) */
	outb(UART_LCR, 0x80);

	/* Set divisor to 1 (115200 baud) */
	outb(UART_DLL, 0x01);
	outb(UART_DLH, 0x00);

	/* 8 bits, no parity, one stop bit (8N1) */
	outb(UART_LCR, 0x03);

	/* Enable FIFO, clear them, with 14-byte threshold */
	outb(UART_FCR, 0xC7);

	/* DTR + RTS + OUT2 */
	outb(UART_MCR, 0x0B);
}

void uart_putc(const char c)
{
	/* Wait for transmit buffer to be empty (LSR bit 5) */
	while ((inb(UART_LSR) & 0x20) == 0)
		;
	outb(UART_THR, c);
}

void uart_puthex(uint64_t n)
{
	const char *hexdigits = "0123456789ABCDEF";

	uart_putc('0');
	uart_putc('x');
	for (int i = 60; i >= 0; i -= 4) {
		uart_putc(hexdigits[(n >> i) & 0xf]);
		if (i == 32)
			uart_putc(' ');
	}
}

void uart_puts(const char *s)
{
	for (int i = 0; s[i] != '\0'; i++)
		uart_putc((unsigned char)s[i]);
}
