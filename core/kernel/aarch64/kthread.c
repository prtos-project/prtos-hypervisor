/*
 * FILE: kthread.c
 *
 * AArch64 kernel thread arch-dependent code
 *
 * http://www.prtos.org/
 */

#include <assert.h>
#include <rsvmem.h>
#include <gaccess.h>
#include <kthread.h>
#include <arch/layout.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include "prtos_vgic.h"
#include <spinlock.h>
#include <stdc.h>
#include <vmmap.h>
#include <arch/prtos_def.h>

void switch_kthread_arch_pre(kthread_t *new, kthread_t *current) {
#ifdef CONFIG_AARCH64
    if (!new->ctrl.g) {
        /* Switching from partition to idle: save guest CNTV state and
         * disable it so the idle loop can use CNTV for WFI wakeup. */
        if (current->ctrl.g) {
            asm volatile("mrs %0, cntv_ctl_el0\n\t"
                         "mrs %1, cntv_cval_el0\n\t"
                         : "=r"(current->ctrl.g->karch.cntv_ctl_el0),
                           "=r"(current->ctrl.g->karch.cntv_cval_el0));
            asm volatile("msr cntv_ctl_el0, xzr\n\tisb" ::: "memory");
        }

        /* Save GICv3 virtual CPU interface state (ICH_LR + control regs)
         * Disable virtual interface & stage-2 MMU */
        if (current->ctrl.g && current->ctrl.g->karch.vgic) {
            prtos_u64_t v0, v1, v2, v3, vh, vm;
            asm volatile("mrs %0, S3_4_C12_C12_0\n\t"
                         "mrs %1, S3_4_C12_C12_1\n\t"
                         "mrs %2, S3_4_C12_C12_2\n\t"
                         "mrs %3, S3_4_C12_C12_3\n\t"
                         "mrs %4, S3_4_C12_C11_0\n\t"
                         "mrs %5, S3_4_C12_C11_7\n\t"
                         : "=r"(v0), "=r"(v1), "=r"(v2), "=r"(v3),
                           "=r"(vh), "=r"(vm));
            current->ctrl.g->karch.ich_lr[0] = v0;
            current->ctrl.g->karch.ich_lr[1] = v1;
            current->ctrl.g->karch.ich_lr[2] = v2;
            current->ctrl.g->karch.ich_lr[3] = v3;
            current->ctrl.g->karch.ich_hcr  = vh;
            current->ctrl.g->karch.ich_vmcr = vm;
            /* Disable virtual CPU interface and clear LRs for idle */
            asm volatile("msr S3_4_C12_C11_0, xzr\n\t"
                         "msr S3_4_C12_C12_0, xzr\n\t"
                         "msr S3_4_C12_C12_1, xzr\n\t"
                         "msr S3_4_C12_C12_2, xzr\n\t"
                         "msr S3_4_C12_C12_3, xzr\n\t"
                         "isb" ::: "memory");
        } else if (current->ctrl.g) {
            /* Para-virt partition: save ICH_LR and ICH_VMCR */
            asm volatile("mrs %0, S3_4_C12_C12_0\n\t"
                         "mrs %1, S3_4_C12_C12_1\n\t"
                         "mrs %2, S3_4_C12_C12_2\n\t"
                         "mrs %3, S3_4_C12_C12_3\n\t"
                         : "=r"(current->ctrl.g->karch.ich_lr[0]),
                           "=r"(current->ctrl.g->karch.ich_lr[1]),
                           "=r"(current->ctrl.g->karch.ich_lr[2]),
                           "=r"(current->ctrl.g->karch.ich_lr[3]));
            asm volatile("mrs %0, S3_4_C12_C11_7\n\t"
                         : "=r"(current->ctrl.g->karch.ich_vmcr));
        }

        /* Disable stage-2 MMU */
        asm volatile("msr vttbr_el2, xzr\n\t"
                     "mrs x10, hcr_el2\n\t"
                     "bic x10, x10, #1\n\t"
                     "msr hcr_el2, x10\n\t"
                     "isb" ::: "x10", "memory");
    }
#endif
}

void switch_kthread_arch_post(kthread_t *current) {
#ifdef CONFIG_AARCH64
    if (current->ctrl.g) {
        /* Restore per-partition stage-2 MMU */
        prtos_u64_t vttbr = current->ctrl.g->karch.vttbr_el2;
        if (vttbr) {
            __asm__ __volatile__(
                "msr vttbr_el2, %0\n\t"
                "dsb ish\n\t"
                "tlbi alle1is\n\t"
                "dsb ish\n\t"
                "isb\n\t"
                : : "r"(vttbr) : "memory");

            /* Virtualize MPIDR so each partition's vCPUs see physical
             * CPU IDs matching DTS reg values.  Using the physical CPU ID
             * ensures that ICC_SGI1_EL1 (which routes using physical
             * MPIDR affinity) delivers SGIs to the correct CPUs.
             * VMPIDR_EL2 bit 31 is RES1 (indicates multiprocessor). */
            {
                int cpu_id = GET_CPU_ID();
                prtos_u64_t vmpidr = (1ULL << 31) | (prtos_u64_t)cpu_id;
                __asm__ __volatile__(
                    "msr vmpidr_el2, %0\n\t"
                    "isb\n\t"
                    : : "r"(vmpidr) : "memory");
            }

            prtos_u64_t hcr = PRTOS_HCR_EL2_VAL;
            __asm__ __volatile__(
                "msr hcr_el2, %0\n\t"
                "isb\n\t"
                : : "r"(hcr) : "memory");

            if (current->ctrl.g->karch.vgic) {
                /* Hw-virt partition: restore full ICH state */
                prtos_u64_t lr0 = current->ctrl.g->karch.ich_lr[0];
                prtos_u64_t lr1 = current->ctrl.g->karch.ich_lr[1];
                prtos_u64_t lr2 = current->ctrl.g->karch.ich_lr[2];
                prtos_u64_t lr3 = current->ctrl.g->karch.ich_lr[3];
                prtos_u64_t hcr_ich = current->ctrl.g->karch.ich_hcr;
                prtos_u64_t vmcr = current->ctrl.g->karch.ich_vmcr;
                if (!hcr_ich) hcr_ich = 0x1;
                if (!vmcr) vmcr = 0xFF000002ULL;
                __asm__ __volatile__(
                    "msr S3_4_C12_C12_0, %0\n\t"
                    "msr S3_4_C12_C12_1, %1\n\t"
                    "msr S3_4_C12_C12_2, %2\n\t"
                    "msr S3_4_C12_C12_3, %3\n\t"
                    "msr S3_4_C12_C11_0, %4\n\t"
                    "msr S3_4_C12_C11_7, %5\n\t"
                    "isb\n\t"
                    : : "r"(lr0), "r"(lr1), "r"(lr2), "r"(lr3),
                        "r"(hcr_ich), "r"(vmcr) : "memory");

                /* Timer virtualization: zero CNTVOFF, allow EL1 to read counter */
                __asm__ __volatile__(
                    "msr CNTVOFF_EL2, xzr\n\t"
                    "mov x10, #1\n\t"
                    "msr CNTHCTL_EL2, x10\n\t"
                    "isb\n\t"
                    ::: "x10", "memory");

                /* Restore guest CNTV state that was saved in
                 * switch_kthread_arch_pre when switching to idle. */
                __asm__ __volatile__(
                    "msr cntv_cval_el0, %0\n\t"
                    "msr cntv_ctl_el0, %1\n\t"
                    "isb\n\t"
                    : : "r"(current->ctrl.g->karch.cntv_cval_el0),
                        "r"(current->ctrl.g->karch.cntv_ctl_el0)
                    : "memory");

                /* If the guest virtual timer fired while partition was off,
                 * mark PPI 27 pending so the flush injects it via ICH_LR. */
                {
                    prtos_u64_t cntv_ctl;
                    __asm__ __volatile__("mrs %0, cntv_ctl_el0" : "=r"(cntv_ctl));
                    if ((cntv_ctl & 0x5) == 0x5) { /* ENABLE=1, ISTATUS=1 */
                        int vcpu_id = KID2VCPUID(current->ctrl.g->id);
                        current->ctrl.g->karch.vgic->vcpu[vcpu_id].ppis[11].pending = 1;
                    }
                }

                /* Flush pending VGIC IRQs into free LR slots */
                extern void prtos_vgic_flush_lrs_current(void);
                prtos_vgic_flush_lrs_current();

                /* Re-enable any physical SPIs that were disabled during
                 * interrupt forwarding. The guest has had a chance to
                 * process and EOI the virtual IRQ. */
                if (current->ctrl.g->karch.spi_fwd_mask[0] |
                    current->ctrl.g->karch.spi_fwd_mask[1] |
                    current->ctrl.g->karch.spi_fwd_mask[2] |
                    current->ctrl.g->karch.spi_fwd_mask[3]) {
                    int wi;
                    for (wi = 0; wi < 4; wi++) {
                        prtos_u64_t mask = current->ctrl.g->karch.spi_fwd_mask[wi];
                        current->ctrl.g->karch.spi_fwd_mask[wi] = 0;
                        int bit;
                        for (bit = 0; bit < 64 && mask; bit++) {
                            if (mask & (1ULL << bit)) {
                                prtos_u32_t intid = (wi * 64 + bit) + 32;
                                volatile prtos_u32_t *gicd_isenabler =
                                    (volatile prtos_u32_t *)(GIC_DIST_BASE + 0x100 + 4 * (intid / 32));
                                *gicd_isenabler = (1U << (intid % 32));
                                mask &= ~(1ULL << bit);
                            }
                        }
                    }
                }
            } else {
                /* Para-virt partition: restore ICH_LR and enable interface */
                prtos_u64_t lr0 = current->ctrl.g->karch.ich_lr[0];

                /* Restore guest CNTV state */
                __asm__ __volatile__(
                    "msr cntv_cval_el0, %0\n\t"
                    "msr cntv_ctl_el0, %1\n\t"
                    "isb\n\t"
                    : : "r"(current->ctrl.g->karch.cntv_cval_el0),
                        "r"(current->ctrl.g->karch.cntv_ctl_el0)
                    : "memory");

                /* If the guest's virtual timer (CNTV) has fired while the
                 * partition was not running, the trap handler masked IMASK
                 * to prevent re-fire.  Detect this by checking ISTATUS and
                 * inject the virtual IRQ now, clearing IMASK so the guest
                 * can service it. */
                {
                    prtos_u64_t cntv_ctl;
                    __asm__ __volatile__("mrs %0, cntv_ctl_el0" : "=r"(cntv_ctl));
                    if ((cntv_ctl & 0x7) == 0x7) { /* ENABLE=1, IMASK=1, ISTATUS=1 */
                        /* Timer fired while partition wasn't running.
                         * Inject virtual PPI 27 via ICH_LR.  Leave IMASK set;
                         * the guest's vClearTickInterrupt() will reload TVAL
                         * (clearing ISTATUS) and then clear IMASK. */
                        lr0 = (1ULL << 62) | (1ULL << 60) | (0xA0ULL << 48) | 27;
                    }
                }

                __asm__ __volatile__(
                    "msr S3_4_C12_C12_0, %0\n\t"
                    "msr S3_4_C12_C12_1, %1\n\t"
                    "msr S3_4_C12_C12_2, %2\n\t"
                    "msr S3_4_C12_C12_3, %3\n\t"
                    "isb\n\t"
                    : : "r"(lr0),
                        "r"(current->ctrl.g->karch.ich_lr[1]),
                        "r"(current->ctrl.g->karch.ich_lr[2]),
                        "r"(current->ctrl.g->karch.ich_lr[3]));
                __asm__ __volatile__(
                    "msr S3_4_C12_C11_0, %0\n\t"
                    "msr S3_4_C12_C11_7, %1\n\t"
                    "isb\n\t"
                    : : "r"((prtos_u64_t)0x1), "r"((prtos_u64_t)0xFF000002ULL)
                    : "memory");
                /* Timer access: allow EL1 to read physical counter */
                __asm__ __volatile__(
                    "msr CNTVOFF_EL2, xzr\n\t"
                    "mov x10, #1\n\t"
                    "msr CNTHCTL_EL2, x10\n\t"
                    "isb\n\t"
                    ::: "x10", "memory");
            }
        }
    }
#endif
}

extern void kthread_startup_wrapper(void);

void setup_kstack(kthread_t *k, void *start_up, prtos_address_t entry_point) {
    prtos_u64_t *sp = (prtos_u64_t *)(&k->kstack[CONFIG_KSTACK_SIZE]);
    *(--sp) = 0ULL;  /* padding for 16-byte alignment */
    *(--sp) = (prtos_u64_t)0;  /* x30 (LR) */
    *(--sp) = (prtos_u64_t)kthread_startup_wrapper; /* resume PC (stored as x9) */
    *(--sp) = 0ULL;                                 /* x29 (FP) */
    *(--sp) = 0ULL;                                 /* x28 */
    *(--sp) = 0ULL;                                 /* x27 */
    *(--sp) = 0ULL;                                 /* x26 */
    *(--sp) = 0ULL;                                 /* x25 */
    *(--sp) = 0ULL;                                 /* x24 */
    *(--sp) = 0ULL;                                 /* x23 */
    *(--sp) = 0ULL;                                 /* x22 */
    *(--sp) = 0ULL;                                 /* x21 */
    *(--sp) = (prtos_u64_t)entry_point;             /* x20 */
    *(--sp) = (prtos_u64_t)start_up;                /* x19 */
    k->ctrl.kstack = (prtos_address_t *)sp;
}

void kthread_arch_init(kthread_t *k) {
}

void setup_kthread_arch(kthread_t *k) {
}

void setup_pct_arch(partition_control_table_t *part_ctrl_table, kthread_t *k) {
}
