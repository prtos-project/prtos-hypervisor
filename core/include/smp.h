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
#endif

#endif
