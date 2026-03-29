/*
 * uart.c - 16550 UART driver for QEMU RISC-V virt
 *
 * QEMU riscv64 virt uses a 16550-compatible UART at 0x10000000.
 */

#include "board.h"
#include "uart.h"

#define UART_THR  (*(volatile unsigned char *)(UART_BASE + 0))  /* Transmit Holding */
#define UART_LSR  (*(volatile unsigned char *)(UART_BASE + 5))  /* Line Status */
#define UART_LSR_THRE  0x20  /* Transmitter Holding Register Empty */

void uart_init(void)
{
	/* QEMU 16550 is ready to use by default, no special init needed */
}

void uart_putc(char c)
{
	while (!(UART_LSR & UART_LSR_THRE))
		;
	UART_THR = c;
}

void uart_puts(const char *s)
{
	while (*s) {
		if (*s == '\n')
			uart_putc('\r');
		uart_putc(*s++);
	}
}

void uart_puthex(unsigned long n)
{
	const char *hex = "0123456789ABCDEF";
	int i;

	uart_puts("0x");
	for (i = 60; i >= 0; i -= 4)
		uart_putc(hex[(n >> i) & 0xF]);
}
