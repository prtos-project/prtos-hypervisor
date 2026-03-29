/*
 * FILE: processor.c
 *
 * RISC-V 64-bit processor management
 *
 * www.prtos.org
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

prtos_u32_t __arch_get_local_id(void) {
    prtos_u32_t id;
    __asm__ __volatile__("mv %0, tp" : "=r"(id));
    return id;
}

prtos_u32_t __arch_get_local_hw_id(void) {
    prtos_u32_t id;
    __asm__ __volatile__("mv %0, tp" : "=r"(id));
    return id;
}

void __arch_set_local_id(prtos_u32_t id) {
    __asm__ __volatile__("mv tp, %0" : : "r"((unsigned long)id));
}

void __arch_set_local_hw_id(prtos_u32_t hw_id) {
    /* On RISC-V QEMU virt, hartid == logical id */
}

local_processor_t *get_local_processor(void) {
    return GET_LOCAL_PROCESSOR();
}

__VBOOT prtos_u32_t calculate_cpu_freq(void) {
    return 10000000;  /* 10 MHz on QEMU */
}
