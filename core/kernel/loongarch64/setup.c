/*
 * FILE: setup.c
 *
 * Setting up and starting up the kernel (LoongArch 64-bit arch dependent part)
 *
 * http://www.prtos.org/
 */

#include <assert.h>
#include <boot.h>
#include <smp.h>
#include <stdc.h>
#include <physmm.h>
#include <irqs.h>

volatile prtos_s8_t local_info_init = 0;

/* Global LVZ availability flag */
prtos_u32_t prtos_lvz_available = 0;

/* Detect and initialize LoongArch LVZ (Virtualization extension) */
static void __VBOOT detect_lvz(void) {
    prtos_u32_t cpucfg2;
    __asm__ __volatile__("cpucfg %0, %1" : "=r"(cpucfg2) : "r"(2));
    if (cpucfg2 & CPUCFG2_LVZP) {
        prtos_lvz_available = 1;
        kprintf("[PRTOS] LVZ virtualization extension detected\n");
    } else {
        prtos_lvz_available = 0;
        kprintf("[PRTOS] LVZ not available, using PLV3 trap-and-emulate\n");
    }
}

static void __VBOOT init_lvz(void) {
    if (!prtos_lvz_available) return;

    /* Initialize LVZ CSRs: GCFG, GSTAT, GINTC, GTLBC */
    __asm__ __volatile__("csrwr %0, 0x51" : : "r"(0UL));  /* GCFG = 0 */
    __asm__ __volatile__("csrwr %0, 0x50" : : "r"(0UL));  /* GSTAT = 0 */
    __asm__ __volatile__("csrwr %0, 0x52" : : "r"(0UL));  /* GINTC = 0 */

    /* Clear GTLBC USETGID and TOTI bits */
    {
        prtos_u64_t gtlbc;
        __asm__ __volatile__("csrrd %0, 0x15" : "=r"(gtlbc));
        gtlbc &= ~(CSR_GTLBC_USETGID | CSR_GTLBC_TOTI);
        __asm__ __volatile__("csrwr %0, 0x15" : "+r"(gtlbc));
    }

    /* Configure GCFG:
     * MATC = ROOT (root-controlled address translation)
     * TOP = 0 (use QEMU shadow CSR optimization for performance;
     *        critical CSRs trap via guest_csr_*_needs_trap lists)
     * TOE = 0 (delegate exceptions to guest)
     * TIT = 0 (delegate timer to guest)
     * GCI = SECURE (trap certain cache instructions)
     */
    {
        prtos_u64_t gcfg = 0;
        prtos_u64_t env;
        __asm__ __volatile__("csrrd %0, 0x51" : "=r"(env));
        if (env & CSR_GCFG_MATP_ROOT)
            gcfg |= CSR_GCFG_MATC_ROOT;
        __asm__ __volatile__("csrwr %0, 0x51" : "+r"(gcfg));
    }

    /* Enable GTLBC.USETGID for TLB guest ID tagging */
    {
        prtos_u64_t gtlbc;
        __asm__ __volatile__("csrrd %0, 0x15" : "=r"(gtlbc));
        gtlbc |= CSR_GTLBC_USETGID;
        __asm__ __volatile__("csrwr %0, 0x15" : "+r"(gtlbc));
    }

    /* Clear LVZ active flag in SAVE5 */
    __asm__ __volatile__("csrwr $zero, 0x35" ::: "memory");

    kprintf("[PRTOS] LVZ initialized: GCFG=0x%llx GTLBC=0x%llx\n",
            ({prtos_u64_t v; __asm__("csrrd %0,0x51":"=r"(v)); v;}),
            ({prtos_u64_t v; __asm__("csrrd %0,0x15":"=r"(v)); v;}));
}

void __VBOOT setup_arch_local(prtos_s32_t cpu_id) {
    SET_CPU_ID(cpu_id);
    SET_CPU_HWID(cpu_id);
}

void __VBOOT early_setup_arch_common(void) {
    SET_NRCPUS(1);
}

prtos_u32_t __VBOOT get_gpu_khz(void) {
    /* LoongArch stable counter frequency: 100MHz on QEMU virt */
    return 100000;  /* 100 MHz in KHz */
}

void __VBOOT setup_arch_common(void) {
    /* Initialize timer */
    extern void init_loongarch_timer(void);
    init_loongarch_timer();

    cpu_khz = get_gpu_khz();
    SET_NRCPUS(1);

    /* Detect and init LVZ */
    detect_lvz();
    init_lvz();
}

void __VBOOT early_delay(prtos_u32_t cycles) {
    volatile prtos_u32_t i;
    for (i = 0; i < cycles; i++)
        ;
}
