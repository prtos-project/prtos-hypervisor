/*
 * FILE: serial.c
 *
 * RISC-V 64 RSW serial output via 16550 UART (QEMU virt)
 *
 * www.prtos.org
 */

#define UART_BASE   ((volatile unsigned char *)0x10000000UL)

#define UART_THR    0   /* Transmit Holding Register */
#define UART_LSR    5   /* Line Status Register */
#define UART_LSR_THRE 0x20  /* Transmit Holding Register Empty */

#ifdef CONFIG_OUTPUT_ENABLED

void init_output(void) {
    /* UART is already initialized by OpenSBI */
}

void xputchar(int c) {
    while (!(UART_BASE[UART_LSR] & UART_LSR_THRE))
        ;
    UART_BASE[UART_THR] = (unsigned char)c;
}

#else
void init_output(void) {}
void xputchar(int c) { (void)c; }
#endif
