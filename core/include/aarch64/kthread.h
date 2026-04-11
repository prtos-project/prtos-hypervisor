/*
 * FILE: kthread.h
 *
 * AArch64 arch kernel thread
 *
 * http://www.prtos.org/
 */

#ifndef _PRTOS_ARCH_KTHREAD_H_
#define _PRTOS_ARCH_KTHREAD_H_

#include <arch/processor.h>

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

struct prtos_vgic_state;  /* forward declaration */

struct cpu_info {
    struct cpu_user_regs guest_cpu_user_regs;
    unsigned long elr;
    prtos_u32_t flags;
};

struct kthread_arch {
    struct {
        prtos_u64_t x19;
        prtos_u64_t x20;
        prtos_u64_t x21;
        prtos_u64_t x22;
        prtos_u64_t x23;
        prtos_u64_t x24;
        prtos_u64_t x25;
        prtos_u64_t x26;
        prtos_u64_t x27;
        prtos_u64_t x28;
        prtos_u64_t x29;  /* FP */
        prtos_u64_t x30;  /* LR */
        prtos_u64_t sp;
    } saved_context;

    void *stack;
    struct cpu_info *cpu_info;

    /* EL1 guest state */
    prtos_u64_t sctlr_el1;
    prtos_u64_t ttbr0_el1;
    prtos_u64_t ttbr1_el1;
    prtos_u64_t tcr_el1;
    prtos_u64_t mair_el1;
    prtos_u64_t vbar_el1;
    prtos_u64_t sp_el1;
    prtos_u64_t elr_el1;
    prtos_u64_t spsr_el1;
    prtos_u64_t far_el1;
    prtos_u64_t esr_el1;
    prtos_u64_t contextidr_el1;
    prtos_u64_t cpacr_el1;

    /* EL2 virtualization control */
    prtos_u64_t hcr_el2;
    prtos_u64_t vttbr_el2;  /* Stage-2 translation table base */

    /* Stage-2 page table pointers */
    prtos_u64_t *s2_l1;     /* Root L1 table, page-aligned */
    prtos_u64_t *s2_l2[2];  /* Pre-allocated L2 tables */
    prtos_u64_t *s2_l3[8];  /* Pre-allocated L3 tables for 4KB page mappings */
    prtos_s32_t s2_l3_count;

    /* VGIC state (for hw-virt partitions, NULL for para-virt) */
    struct prtos_vgic_state *vgic;

    /* GICv3 virtual CPU interface state saved/restored across context switches */
    prtos_u64_t ich_lr[4];    /* ICH_LR0_EL2 .. ICH_LR3_EL2 */
    prtos_u64_t ich_hcr;      /* ICH_HCR_EL2 */
    prtos_u64_t ich_vmcr;     /* ICH_VMCR_EL2 */

    /* Bitmask of forwarded physical SPIs that need re-enabling at GICD.
     * Array of 4 x 64-bit words supports SPIs 0-255 (GIC intid 32-287). */
    prtos_u64_t spi_fwd_mask[4];

    /* Guest timer emulation */
    prtos_u64_t guest_timer_active;
    prtos_u64_t cntv_cval_el0;
    prtos_u64_t cntv_ctl_el0;
    prtos_u64_t cntvoff_el2;

    /* Virtual interrupt pending */
    prtos_u64_t virq_pending;

    /* PSCI emulation (for HW-virt multi-vCPU partitions) */
    prtos_u64_t psci_entry;      /* Entry point set by PSCI CPU_ON */
    prtos_u64_t psci_context_id; /* Context ID passed as x0 */
};

#endif
