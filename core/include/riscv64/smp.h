/*
 * FILE: smp.h
 *
 * RISC-V 64-bit SMP related stuff
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_SMP_H_
#define _PRTOS_ARCH_SMP_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <linkage.h>

#ifdef CONFIG_SMP
/*
 * These functions send IPIs using SBI. They handle the
 * logical-to-physical hartid mapping internally.
 */
extern void riscv_send_ipi_to(unsigned long cpu);
extern void riscv_send_ipi_all_others(void);
#endif /* CONFIG_SMP */

#endif
