/*
 * FILE: processor.c
 *
 * AArch64 processor management
 *
 * http://www.prtos.org/
 */

#include <assert.h>
#include <processor.h>
#include <local.h>

struct local_id local_id_table[CONFIG_NO_CPUS];
prtos_u32_t cpu_features;
void (*Idle)(void);

void _reset(prtos_address_t addr) {
}

#define __VBOOT
void __VBOOT setup_cpu(void) {
}

#ifdef CONFIG_SMP
void __VBOOT setup_cpu_idtable(prtos_u32_t num_of_cpus) {
    prtos_u32_t e;
    for (e = 0; e < num_of_cpus; e++) {
        local_id_table[e].id = e;
    }
}
#endif

void __VBOOT setup_cr(void) {
}

void __VBOOT setup_gdt(prtos_s32_t cpu_id) {
}

/* Per-CPU ID stored in TPIDR_EL2 */
prtos_u32_t __arch_get_local_id(void) {
    prtos_u64_t id;
    __asm__ __volatile__("mrs %0, tpidr_el2" : "=r"(id));
    return (prtos_u32_t)id;
}

prtos_u32_t __arch_get_local_hw_id(void) {
    prtos_u64_t id;
    __asm__ __volatile__("mrs %0, tpidr_el2" : "=r"(id));
    return (prtos_u32_t)id;
}

void __arch_set_local_id(prtos_u32_t id) {
    __asm__ __volatile__("msr tpidr_el2, %0" : : "r"((prtos_u64_t)id));
}

void __arch_set_local_hw_id(prtos_u32_t hw_id) {
    /* On AArch64 QEMU virt, MPIDR Aff0 == logical id */
}

local_processor_t *get_local_processor(void) {
    return GET_LOCAL_PROCESSOR();
}

__VBOOT prtos_u32_t calculate_cpu_freq(void) {
    prtos_u64_t freq;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
    return (prtos_u32_t)freq;
}
