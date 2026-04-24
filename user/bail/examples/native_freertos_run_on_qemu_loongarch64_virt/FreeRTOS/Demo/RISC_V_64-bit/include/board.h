/*
 * board.h - QEMU LoongArch64 virt machine definitions
 */

#ifndef BOARD_H
#define BOARD_H

/* QEMU loongarch64 virt memory map */
#define UART_BASE           0x1FE001E0UL    /* LoongArch QEMU virt UART */
#define PLIC_BASE           0x00000000UL    /* Not used */
#define CLINT_BASE          0x00000000UL    /* Not used */

/* Timer frequency: 100 MHz on LoongArch64 QEMU virt (stable_counter_freq) */
#define TIMER_FREQ          100000000UL

/* Timer IRQ: bit 11 in ESTAT (TI - timer interrupt) */
#define TIMER_IRQ           11

#endif /* BOARD_H */
