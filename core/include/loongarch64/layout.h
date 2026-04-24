/*
 * FILE: layout.h
 *
 * LoongArch 64-bit memory layout for QEMU virt machine
 *
 * http://www.prtos.org/
 */

#ifndef __LAYOUT_H__
#define __LAYOUT_H__

/* 4K page size */
#define SZ_4K 0x00001000
#define PAGESIZE SZ_4K

/* QEMU LoongArch virt machine: RAM starts at 0x00000000 */
#define PHYBASE 0x00000000ULL
#define PHYSIZE (256 * 1024 * 1024)
#define PHYEND (PHYBASE + PHYSIZE)

/* UART: 16550 at 0x1FE001E0 on QEMU LoongArch virt */
#define UART_BASE 0x800000001FE001E0ULL

/* IPI mailbox base (per-CPU IOCSR addresses) */
#define IOCSR_IPI_STATUS   0x1000
#define IOCSR_IPI_EN       0x1004
#define IOCSR_IPI_SET      0x1008
#define IOCSR_IPI_CLEAR    0x100C
#define IOCSR_MBUF0        0x1020
#define IOCSR_MBUF1        0x1028
#define IOCSR_MBUF2        0x1030
#define IOCSR_MBUF3        0x1038

/* IPI send register (writes to remote CPU) */
#define IOCSR_IPI_SEND     0x1040

/* LoongArch Direct Mapped Window (DMW) bases */
#define DMW_UNCACHED_BASE  0x8000000000000000ULL  /* Strongly-ordered uncached */
#define DMW_CACHED_BASE    0x9000000000000000ULL  /* Coherent cached */
#define DMW_MASK           0xF000000000000000ULL  /* Top 4 bits select DMW */

#endif
