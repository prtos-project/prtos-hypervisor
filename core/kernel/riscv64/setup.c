/*
 * FILE: setup.c
 *
 * Setting up and starting up the kernel (RISC-V 64-bit arch dependent part)
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <smp.h>
#include <stdc.h>
#include <physmm.h>

volatile prtos_s8_t local_info_init = 0;

void __VBOOT setup_arch_local(prtos_s32_t cpu_id) {
    SET_CPU_ID(cpu_id);
    SET_CPU_HWID(cpu_id);
}

void __VBOOT early_setup_arch_common(void) {
    SET_NRCPUS(1);
}

prtos_u32_t __VBOOT get_gpu_khz(void) {
    /* RISC-V timer frequency: 10MHz on QEMU virt */
    return 10000;  /* 10 MHz in KHz */
}

void __VBOOT setup_arch_common(void) {
    /* Initialize timer */
    extern void init_riscv_timer(void);
    init_riscv_timer();

    cpu_khz = get_gpu_khz();
    SET_NRCPUS(1);
}

void __VBOOT early_delay(prtos_u32_t cycles) {
    volatile prtos_u32_t i;
    for (i = 0; i < cycles; i++)
        ;
}
