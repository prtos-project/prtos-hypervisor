/*
 * board.h - QEMU RISC-V 64 virt machine definitions
 */

#ifndef BOARD_H
#define BOARD_H

/* QEMU riscv64 virt memory map */
#define UART_BASE           0x10000000UL    /* 16550 UART */
#define PLIC_BASE           0x0c000000UL    /* PLIC */
#define CLINT_BASE          0x02000000UL    /* CLINT */

/* Timer frequency: 10 MHz on QEMU virt */
#define TIMER_FREQ          10000000UL

/* Timer IRQ: supervisor timer interrupt (scause code 5) */
#define TIMER_IRQ           5

#endif /* BOARD_H */
