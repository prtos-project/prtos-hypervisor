#ifndef __PL011_H__
#define __PL011_H__

#define UART_IRQ_LINE   33

void pl011_putc(char c);
void pl011_puts(char *s);
int  pl011_getc(void);
void pl011_init(void);
void pl011_irq_handler(void);

#endif
