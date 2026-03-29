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

    /* Delegate VS-mode exceptions to guest via hedeleg:
     * bits 0-8: misaligned, access faults, illegal insn, breakpoint, ecall from U
     * bit 12 = instruction page fault, bit 13 = load page fault,
     * bit 15 = store/AMO page fault.
     * Without this, Linux guest MMU page faults trap to HS-mode and crash. */
    /* Delegate VS-mode interrupts to guest via hideleg:
     * bit 2 = VSSIP (Virtual Supervisor Software Interrupt)
     * bit 6 = VSTIP (Virtual Supervisor Timer Interrupt)
     * bit 10 = VSEIP (Virtual Supervisor External Interrupt)
     * Also set hcounteren.TM (bit 1) to allow guest rdtime access. */
    __asm__ __volatile__(
        "li t0, (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 12) | (1 << 13) | (1 << 15)\n\t"
        "csrs hedeleg, t0\n\t"
        "li t0, (1 << 2) | (1 << 6) | (1 << 10)\n\t"
        "csrs hideleg, t0\n\t"
        "li t0, (1 << 1)\n\t"
        "csrs hcounteren, t0\n\t"
        ::: "t0", "memory"
    );
}

void __VBOOT early_delay(prtos_u32_t cycles) {
    volatile prtos_u32_t i;
    for (i = 0; i < cycles; i++)
        ;
}
