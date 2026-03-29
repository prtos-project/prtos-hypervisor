/*
 * FILE: smp.h
 *
 * SMP related stuff
 *
 * www.prtos.org
 */

#ifndef _PRTOS_SMP_H_
#define _PRTOS_SMP_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <linkage.h>
#include <arch/smp.h>
#include <spinlock.h>

// This structure is stored by each processor
struct local_id {
    prtos_u32_t id;    // logical ID
    prtos_u32_t hw_id;  // HW ID
} __PACKED;

extern struct local_id local_id_table[CONFIG_NO_CPUS];
extern prtos_u16_t __nr_cpus;

#define GET_NRCPUS() __nr_cpus
#define SET_NRCPUS(nr_cpu) __nr_cpus = (nr_cpu)

extern prtos_s32_t init_smp(void);
extern void setup_smp(void);

#ifndef CONFIG_SMP
#define smp_flush_tlb()
#define smp_halt_all()
#define CROSS_CPU_SCHED_NOTIFY(cpu) do {} while(0)
#else
#ifdef CONFIG_x86
/* x86: use APIC IPI for cross-CPU notifications; smp_halt_all is in arch/apic.h */
#define CROSS_CPU_SCHED_NOTIFY(cpu) send_ipi((cpu), NO_SHORTHAND_IPI, SCHED_PENDING_IPI_VECTOR)
#elif defined(CONFIG_AARCH64)
#define CROSS_CPU_SCHED_NOTIFY(cpu) do { __asm__ __volatile__("sev" ::: "memory"); } while(0)
#define smp_halt_all() do { __asm__ __volatile__("dsb ish\n\tsev" ::: "memory"); } while(0)
#elif defined(CONFIG_riscv64)
#define CROSS_CPU_SCHED_NOTIFY(cpu) riscv_send_ipi_to(cpu)
#define smp_halt_all() riscv_send_ipi_all_others()
#endif
#endif

#endif
