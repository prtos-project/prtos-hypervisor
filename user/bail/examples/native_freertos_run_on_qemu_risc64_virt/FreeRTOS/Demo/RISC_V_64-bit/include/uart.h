/*
 * uart.h - 16550 UART driver interface for QEMU RISC-V virt
 */

#ifndef UART_H
#define UART_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_puthex(unsigned long n);

/* printf support (implemented in printf-stdarg.c) */
int printf(const char *format, ...);

#endif /* UART_H */
