/*
 * FILE: setup.c
 *
 * Setting up and starting up the kernel (AArch64 arch dependent part)
 *
 * http://www.prtos.org/
 */

#include <assert.h>
#include <boot.h>
#include <smp.h>
#include <stdc.h>
#include <physmm.h>
#include <arch/layout.h>

volatile prtos_s8_t local_info_init = 0;

static void __VBOOT gic_init_percpu(prtos_s32_t cpu_id) {
    /* Redistributor (GICR) — wake up this CPU */
    prtos_u64_t gicr_base = GIC_REDIST_BASE + (prtos_u64_t)cpu_id * GIC_REDIST_STRIDE;
    volatile prtos_u32_t *gicr_waker = (volatile prtos_u32_t *)(gicr_base + 0x14);
    *gicr_waker = *gicr_waker & ~(1 << 1);  /* Clear ProcessorSleep */
    while (*gicr_waker & (1 << 2)) ;         /* Wait ChildrenAsleep clear */

    /* SGI/PPI base is at GICR + 0x10000 */
    prtos_u64_t gicr_sgi_base = gicr_base + 0x10000;
    volatile prtos_u32_t *gicr_sgi = (volatile prtos_u32_t *)gicr_sgi_base;

    /* GICR_IGROUPR0: Set PPI 26, SGI 0, PPI 27 to Group 1 NS */
    gicr_sgi[0x80 / 4] = gicr_sgi[0x80 / 4] | (1 << GIC_PPI_HYP_TIMER) | (1 << GIC_SGI_IPI) | (1 << 27);
    /* GICR_IGRPMODR0: clear bits for NS Group 1 */
    gicr_sgi[0xD00 / 4] = gicr_sgi[0xD00 / 4] & ~((1 << GIC_PPI_HYP_TIMER) | (1 << GIC_SGI_IPI) | (1 << 27));
    /* GICR_ISENABLER0: Enable PPI 26, SGI 0, PPI 27 (guest virtual timer) */
    gicr_sgi[0x100 / 4] = (1 << GIC_PPI_HYP_TIMER) | (1 << GIC_SGI_IPI) | (1 << 27);
    /* Set priority for PPI 26, SGI 0, PPI 27 */
    volatile prtos_u8_t *gicr_pri = (volatile prtos_u8_t *)(gicr_sgi_base + 0x400);
    gicr_pri[GIC_PPI_HYP_TIMER] = 0;  /* Highest priority */
    gicr_pri[GIC_SGI_IPI] = 0;        /* Highest priority */
    gicr_pri[27] = 0;                 /* Highest priority for guest vtimer */

    /* CPU interface (ICC system registers) */
    prtos_u64_t icc_val;
    /* ICC_SRE_EL2: Enable system register access */
    icc_val = 0x7;  /* SRE=1, DFB=1, DIB=1 */
    __asm__ __volatile__("msr S3_4_C12_C9_5, %0\n\tisb" : : "r"(icc_val));

    /* ICC_PMR_EL1: Set priority mask to allow all */
    icc_val = 0xFF;
    __asm__ __volatile__("msr S3_0_C4_C6_0, %0\n\tisb" : : "r"(icc_val));
    /* ICC_IGRPEN0_EL1: Enable Group 0 */
    icc_val = 1;
    __asm__ __volatile__("msr S3_0_C12_C12_6, %0\n\tisb" : : "r"(icc_val));
    /* ICC_IGRPEN1_EL1: Enable Group 1 */
    icc_val = 1;
    __asm__ __volatile__("msr S3_0_C12_C12_7, %0\n\tisb" : : "r"(icc_val));

    /* ICH_HCR_EL2: Enable virtual CPU interface for guest IRQ delivery via ICH_LR */
    {
        prtos_u64_t ich_hcr = 1; /* En = 1 */
        __asm__ __volatile__("msr S3_4_C12_C11_0, %0\n\tisb" : : "r"(ich_hcr));
    }
    /* ICH_VMCR_EL2: VPMR=0xFF (allow all priorities), VENG1=1 (enable Group 1) */
    {
        prtos_u64_t ich_vmcr = (0xFFULL << 24) | (1ULL << 1);
        __asm__ __volatile__("msr S3_4_C12_C11_7, %0\n\tisb" : : "r"(ich_vmcr));
    }

}

void __VBOOT setup_arch_local(prtos_s32_t cpu_id) {
    SET_CPU_ID(cpu_id);
    SET_CPU_HWID(cpu_id);
    if (cpu_id != 0) {
        gic_init_percpu(cpu_id);
        /* VTCR_EL2 is per-CPU: replicate the configuration from CPU 0 */
        extern void setup_vtcr_percpu(void);
        setup_vtcr_percpu();
    }
}

void __VBOOT early_setup_arch_common(void) {
    SET_NRCPUS(1);
}

prtos_u32_t __VBOOT get_gpu_khz(void) {
    /* AArch64 generic timer: read CNTFRQ_EL0 */
    prtos_u64_t freq;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
    return (prtos_u32_t)(freq / 1000);  /* Convert Hz to KHz */
}

void __VBOOT setup_arch_common(void) {
    /* Initialize GICv3 Distributor */
    {
        volatile prtos_u32_t *gicd = (volatile prtos_u32_t *)GIC_DIST_BASE;
        /* GICD_CTLR: Enable ARE_S, Group 0, Group 1S, Group 1NS */
        gicd[0] = (1 << 4) | (1 << 0) | (1 << 1) | (1 << 2);

        /* Enable physical UART SPI (intid 33 = SPI 1) for hw-virt passthrough.
         * GICD_IGROUPR1: set bit 1 = Group 1 NS */
        gicd[0x84 / 4] |= (1 << 1);
        /* GICD_ISENABLER1: enable SPI 33 */
        gicd[0x104 / 4] = (1 << 1);
        /* GICD_IPRIORITYR: set priority 0 (highest) for intid 33 */
        ((volatile prtos_u8_t *)gicd)[0x400 + 33] = 0;
        /* GICD_IROUTER33: route to CPU 0 (Aff0=0) */
        ((volatile prtos_u64_t *)((prtos_u64_t)gicd + 0x6000 + 33 * 8))[0] = 0;
    }

    /* Initialize GICv3 Redistributor + ICC for CPU 0 */
    gic_init_percpu(0);

    /* Initialize timer */
    extern void init_aarch64_timer(void);
    init_aarch64_timer();

    cpu_khz = get_gpu_khz();
    SET_NRCPUS(1);

    /* Configure HCR_EL2 for guest trapping:
     * VM  = 1 (enable stage-2 translation)
     * SWIO = 1 (set-way cache invalidation override)
     * FMO = 1 (physical FIQ routing to EL2)
     * IMO = 1 (physical IRQ routing to EL2)
     * AMO = 1 (SError routing to EL2)
     * TWI = 1 (trap WFI to EL2)
     * RW  = 1 (EL1 is AArch64) */
    /* Note: HCR_EL2 is set per-partition in JMP_PARTITION / switch_kthread_arch_post */
}

void __VBOOT early_delay(prtos_u32_t cycles) {
    volatile prtos_u32_t i;
    for (i = 0; i < cycles; i++)
        ;
}
