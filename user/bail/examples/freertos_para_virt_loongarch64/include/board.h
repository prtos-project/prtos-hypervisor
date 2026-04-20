/*
 * board.h - LoongArch64 QEMU virt board definitions for FreeRTOS
 */
#ifndef BOARD_H
#define BOARD_H

/* UART: 16550 at the address exposed by PRTOS PCT */
/* For para-virt, UART output goes via partition control table */
#define UART_BASE   0x1FE001E0UL  /* LoongArch QEMU virt UART */
#define TIMER_FREQ  100000000UL   /* 100 MHz timer (from PRTOS) */

#endif /* BOARD_H */
