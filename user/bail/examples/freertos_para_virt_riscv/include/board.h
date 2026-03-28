/*
 * board.h - QEMU RISC-V 64 virt machine definitions (para-virt)
 */

#ifndef BOARD_H
#define BOARD_H

/* UART 16550 at 0x10000000 (mapped by PRTOS G-stage) */
#define UART_BASE           0x10000000UL

/* Timer frequency: 10 MHz on QEMU virt (same as native) */
#define TIMER_FREQ          10000000UL

/* Timer IRQ: supervisor timer interrupt (scause code 5) */
#define TIMER_IRQ           5

#endif /* BOARD_H */
