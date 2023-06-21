/*
 * FILE: setup.c
 *
 * Setting up and starting up the kernel (arch dependent part)
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <smp.h>
#include <stdc.h>
#include <physmm.h>
#include <arch/pic.h>

volatile prtos_s8_t localInfoInit = 0;

void __VBOOT setup_arch_local(prtos_s32_t cpu_id) {
    extern void init_lapic(prtos_s32_t cpu_id);

    localInfoInit = 1;
    SET_CPU_ID(cpu_id);
#ifdef CONFIG_APIC
    SET_CPU_HWID(x86_mp_conf.cpu[cpu_id].id);
    init_lapic(x86_mp_conf.cpu[cpu_id].id);
#else
    SET_CPU_HWID(cpu_id);
#endif
}

void __VBOOT early_setup_arch_common(void) {
    extern void setup_x86_idt(void);

    SET_NRCPUS(1);
    setup_x86_idt();
}

prtos_u32_t __VBOOT GetCpuKhz(void) {
    extern prtos_u32_t calculate_cpu_freq(void);
    prtos_u32_t cpu_khz = prtos_conf_table.hpv.cpu_table[GET_CPU_ID()].freq;
    if (cpu_khz == PRTOS_CPUFREQ_AUTO) cpu_khz = calculate_cpu_freq() / 1000;

    return cpu_khz;
}

void __VBOOT setup_arch_common(void) {
#ifdef CONFIG_HPET
    extern void init_hpet(void);

    init_hpet();
#endif
    cpu_khz = GetCpuKhz();
    init_pic(0x20, 0x28);
#ifdef CONFIG_SMP
    SET_NRCPUS(init_smp());
    setup_apic_common();
#endif
}

void __VBOOT EarlyDelay(prtos_u32_t cycles) {
    hw_time_t t, c;
    t = read_tsc_load_low() + cycles;
    do {
        c = read_tsc_load_low();
    } while (t > c);
}
