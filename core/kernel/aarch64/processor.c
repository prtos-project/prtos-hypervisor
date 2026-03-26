/*
 * FILE: processor.c
 *
 * Processor
 *
 * www.prtos.org
 */

#include <assert.h>
// #include <boot.h>
#include <processor.h>
#include <local.h>


#define MAX_CPU_ID 16 /*Should be customized for each processor */

struct local_id local_id_table[CONFIG_NO_CPUS];
prtos_u32_t cpu_features;
void (*Idle)(void);

void _reset(prtos_address_t addr) {
}
#define __VBOOT 
 void __VBOOT setup_cpu(void) {}

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

// prtos_u32_t __arch_get_local_id(void) {
//     prtos_u32_t id = 0;
//     // __asm__ __volatile__("mov %%gs:0, %0\n\t" : "=r"(id));
//     return id;
// }

prtos_u32_t __arch_get_local_hw_id(void) {
    prtos_u32_t hw_id=0;
    // __asm__ __volatile__("mov %%gs:4, %0\n\t" : "=r"(hw_id));
    return hw_id;
}

void __arch_set_local_id(prtos_u32_t id) {
    // __asm__ __volatile__("movl %0, %%gs:0\n\t" ::"r"(id));
}

void __arch_set_local_hw_id(prtos_u32_t hw_id) {
    // __asm__ __volatile__("movl %0, %%gs:4\n\t" ::"r"(hw_id));
}

local_processor_t *get_local_processor() {
    return GET_LOCAL_PROCESSOR();
}

#define CLOCK_TICK_RATE 1193180 /* Underlying HZ */
#define PIT_CH2 0x42
#define PIT_MODE 0x43
#define CALIBRATE_MULT 100
#define CALIBRATE_CYCLES CLOCK_TICK_RATE / CALIBRATE_MULT

__VBOOT prtos_u32_t calculate_cpu_freq(void) {
    prtos_u64_t c_start, c_stop, delta;


    return (c_stop - (c_start + delta)) * CALIBRATE_MULT;
}
