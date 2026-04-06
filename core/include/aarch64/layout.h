/*
 * FILE: layout.h
 *
 * AArch64 memory layout for QEMU virt machine
 *
 * http://www.prtos.org/
 */

#ifndef __LAYOUT_H__
#define __LAYOUT_H__


/* 4K page size */
#define SZ_4K 0x00001000
#define PAGESIZE SZ_4K

/* QEMU AArch64 virt machine: RAM at 0x40000000 */
#define PHYBASE 0x40000000ULL
#define PHYSIZE (256 * 1024 * 1024)
#define PHYEND (PHYBASE + PHYSIZE)

/* PL011 UART at 0x09000000 on QEMU virt */
#define PL011BASE 0x09000000ULL
#define UART_BASE PL011BASE

/* GIC-400 base addresses on QEMU virt */
#define GIC_DIST_BASE  0x08000000ULL  /* Distributor */
#define GIC_CPU_BASE   0x08010000ULL  /* CPU interface (GICv2 compat) */
#define GIC_HYPC_BASE  0x08030000ULL  /* Hypervisor control (GICv2) */
#define GIC_VCPU_BASE  0x08040000ULL  /* Virtual CPU interface (GICv2) */
#define GIC_REDIST_BASE 0x080a0000ULL /* Redistributor (GICv3, QEMU virt) */
#define GIC_REDIST_STRIDE 0x20000ULL  /* Per-CPU redistributor size */

/* GIC interrupt IDs */
#define GIC_PPI_VTIMER   27  /* Virtual timer PPI (ID 27) */
#define GIC_PPI_PTIMER   30  /* Physical timer PPI (ID 30) */
#define GIC_PPI_HYP_TIMER 26 /* Hypervisor timer PPI (ID 26) */
#define GIC_SGI_IPI      0   /* SGI #0 for IPI */

#endif
