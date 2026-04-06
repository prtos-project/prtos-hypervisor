/*
 * FILE: smp.c
 *
 * AArch64 SMP support using PSCI
 *
 * http://www.prtos.org/
 */

#include <processor.h>
#include <stdc.h>

#ifdef CONFIG_SMP
#include <boot.h>
#include <smp.h>
#include <arch/layout.h>

extern void setup_cpu_idtable(prtos_u32_t num_of_cpus);
extern void _secondary_start(void);
extern prtos_u32_t _boot_cpuid;
extern prtos_u32_t _boot_cpuid_va;
extern unsigned long _secondary_start_addr;
extern unsigned long _secondary_start_addr_va;

/*
 * PSCI CPU_ON (SMC32 convention):
 *   Function ID: 0xC4000003 (PSCI_0_2_FN64_CPU_ON)
 *   x1 = target MPIDR
 *   x2 = entry_point_address (physical)
 *   x3 = context_id (passed as x0 to target)
 */
static long psci_cpu_on(unsigned long target_mpidr, unsigned long entry_point,
                        unsigned long context_id) {
    register unsigned long x0 __asm__("x0") = 0xC4000003UL;  /* CPU_ON 64-bit */
    register unsigned long x1 __asm__("x1") = target_mpidr;
    register unsigned long x2 __asm__("x2") = entry_point;
    register unsigned long x3 __asm__("x3") = context_id;
    __asm__ __volatile__("smc #0"
                         : "+r"(x0)
                         : "r"(x1), "r"(x2), "r"(x3)
                         : "memory");
    return (long)x0;
}

void __VBOOT setup_smp(void) {
    prtos_u32_t ncpus, cpu;
    long ret;

    ncpus = prtos_conf_table.hpv.num_of_cpus;
    if (ncpus > CONFIG_NO_CPUS)
        ncpus = CONFIG_NO_CPUS;
    SET_NRCPUS(ncpus);

    if (ncpus <= 1)
        return;

    setup_cpu_idtable(ncpus);

    /* Use _secondary_start_addr_va (VA alias of _secondary_start_addr in .boot.data)
     * to get the physical address stored at boot time. */
    for (cpu = 1; cpu < ncpus; cpu++) {
        kprintf("Starting secondary CPU %d\n", cpu);
        /* MPIDR = cpu (Aff0), context_id = cpu (logical CPU ID) */
        ret = psci_cpu_on(cpu, _secondary_start_addr_va, cpu);
        if (ret != 0)
            kprintf("Failed to start CPU %d: error %ld\n", cpu, ret);
    }
}

prtos_s32_t __VBOOT init_smp(void) {
    return 0;
}

/*
 * Send IPI via GICv3 ICC_SGI1R_EL1
 * SGI INTID = GIC_SGI_IPI (0)
 * Target specified by Aff0 target list
 */
void aarch64_send_ipi_to(unsigned long cpu) {
    /* ICC_SGI1R_EL1: Aff3=0, Aff2=0, Aff1=0, INTID in bits[27:24], TargetList in bits[15:0] */
    prtos_u64_t val = ((prtos_u64_t)GIC_SGI_IPI << 24) | (1UL << cpu);
    __asm__ __volatile__("msr S3_0_C12_C11_5, %0\n\tisb" : : "r"(val));  /* ICC_SGI1R_EL1 */
}

void aarch64_send_ipi_all_others(void) {
    /* ICC_SGI1R_EL1 with IRM=1 (all PEs other than self) */
    prtos_u64_t val = ((prtos_u64_t)GIC_SGI_IPI << 24) | (1ULL << 40);  /* IRM bit */
    __asm__ __volatile__("msr S3_0_C12_C11_5, %0\n\tisb" : : "r"(val));  /* ICC_SGI1R_EL1 */
}

#else
prtos_s32_t __VBOOT init_smp(void) {
    return 0;
}
#endif /* CONFIG_SMP */
