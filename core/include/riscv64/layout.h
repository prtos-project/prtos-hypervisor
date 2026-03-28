/*
 * FILE: layout.h
 *
 * RISC-V 64-bit memory layout for QEMU virt machine
 *
 * www.prtos.org
 */

#ifndef __LAYOUT_H__
#define __LAYOUT_H__

#define NCPU 1

/* 4K page size */
#define SZ_4K 0x00001000
#define PAGESIZE SZ_4K

/* QEMU RISC-V virt machine: RAM at 0x80000000 */
#define PHYBASE 0x80000000ULL
#define PHYSIZE (128 * 1024 * 1024)
#define PHYEND (PHYBASE + PHYSIZE)

/* UART: 16550 at 0x10000000 on QEMU virt */
#define UART_BASE 0x10000000ULL

/* PLIC base address on QEMU virt */
#define PLIC_BASE 0x0c000000ULL
#define PLIC_SIZE 0x4000000

/* CLINT base address on QEMU virt */
#define CLINT_BASE 0x02000000ULL
#define CLINT_SIZE 0x10000

#endif
