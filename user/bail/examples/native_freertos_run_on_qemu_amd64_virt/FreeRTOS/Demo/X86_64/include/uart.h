#ifndef _UART_H
#define _UART_H
#include <stdint.h>
#include "board.h"

void uart_init(void);
void uart_putc(const char c);
void uart_puthex(uint64_t n);
void uart_puts(const char *s);
int printf(const char *format, ...);
int sprintf(char *out, const char *format, ...);
#ifdef TEST_PRINTF
int test_printf(void);
#endif

#endif /* _UART_H */
