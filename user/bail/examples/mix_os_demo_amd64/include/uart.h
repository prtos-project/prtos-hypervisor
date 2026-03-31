#ifndef _UART_H
#define _UART_H

#include <stdint.h>

void uart_init(void);
void uart_putc(const char c);
void uart_puthex(uint64_t n);
void uart_puts(const char *s);
int printf(const char *format, ...);
int sprintf(char *out, const char *format, ...);

#endif
