/*
 * FILE: io.h
 *
 * I/O stubs for RISC-V 64-bit (no x86-style port I/O)
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_IO_H_
#define _PRTOS_ARCH_IO_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

/* RISC-V uses MMIO, no port I/O */
#define io_delay()
#define out_byte(val, port)
#define out_word(val, port)
#define out_line(val, port)
#define out_byte_port(val, port)
#define in_byte(port) 0
#define in_word(port) 0
#define in_line(port) 0
#define in_byte_port(port) 0

#endif
