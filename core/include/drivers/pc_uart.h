/*
 * FILE: uart.h
 *
 * ix86 UART driver
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_UART_H_
#define _PRTOS_ARCH_UART_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <arch/io.h>
#include <arch/prtos_def.h>

#define SPORT0 0x3F8
#define SPORT1 0x2F8
#define SPORT2 0x3E8
#define SPORT3 0x2E8

#define DEFAULT_PORT SPORT0

#define UART_IRQ0 4
#define UART_IRQ1 3

#endif
