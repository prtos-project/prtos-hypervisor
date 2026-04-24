/*
 * FILE: processor.c
 *
 * LoongArch 64-bit processor management
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

prtos_u32_t __arch_get_local_id(void) {
    prtos_u32_t id;
    __asm__ __volatile__("csrrd %0, 0x31" : "=r"(id));  /* CSR_SAVE1 = cpu_id */
    return id;
}

prtos_u32_t __arch_get_local_hw_id(void) {
    prtos_u32_t id;
    __asm__ __volatile__("csrrd %0, 0x20" : "=r"(id));  /* CSR_CPUID */
    return id;
}

void __arch_set_local_id(prtos_u32_t id) {
    __asm__ __volatile__("csrwr %0, 0x31" : "+r"(id));  /* CSR_SAVE1 */
}

void __arch_set_local_hw_id(prtos_u32_t hw_id) {
    /* On LoongArch QEMU virt, CPUID is read-only */
}

local_processor_t *get_local_processor(void) {
    return GET_LOCAL_PROCESSOR();
}

__VBOOT prtos_u32_t calculate_cpu_freq(void) {
    return 100000000;  /* 100 MHz on QEMU */
}
