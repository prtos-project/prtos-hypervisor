#ifndef _BOARD_H
#define _BOARD_H

/* Serial port (COM1) on QEMU x86_64 */
#define UART_BASE               0x3F8

/* PIT (Programmable Interval Timer) */
#define PIT_CH0_DATA            0x40
#define PIT_CMD                 0x43
#define PIT_FREQUENCY           1193182UL

/* Timer IRQ: PIT is IRQ 0, mapped to IDT vector 32 */
#define TIMER_IRQ               32

/* Interrupt controller: 8259 PIC */
#define PIC1_CMD                0x20
#define PIC1_DATA               0x21
#define PIC2_CMD                0xA0
#define PIC2_DATA               0xA1

/* PIC commands */
#define PIC_EOI                 0x20

#endif /* _BOARD_H */
