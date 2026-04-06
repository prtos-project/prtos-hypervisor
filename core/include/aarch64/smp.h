/*
 * FILE: smp.h
 *
 * AArch64 SMP related stuff
 *
 * http://www.prtos.org/
 */

#ifndef _PRTOS_ARCH_SMP_H_
#define _PRTOS_ARCH_SMP_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <linkage.h>

#ifdef CONFIG_SMP
/* Send an IPI via GIC SGI */
extern void aarch64_send_ipi_to(unsigned long cpu);
extern void aarch64_send_ipi_all_others(void);

#define smp_flush_tlb() do { __asm__ __volatile__("dsb ish\n\ttlbi alle2is\n\tdsb ish\n\tisb" ::: "memory"); } while(0)
#endif /* CONFIG_SMP */

#endif
