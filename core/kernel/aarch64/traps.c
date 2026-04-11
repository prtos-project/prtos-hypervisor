/*
 * FILE: traps.c
 *
 * AArch64 trap handler (C part)
 *
 * http://www.prtos.org/
 */

#include <assert.h>
#include <irqs.h>
#include <kthread.h>
#include <ktimer.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <stdc.h>
#include <hypercalls.h>
#include <arch/layout.h>

#include "prtos_vgic.h"

extern void do_hyp_irq(cpu_ctxt_t *ctxt);
extern void do_hyp_trap(cpu_ctxt_t *ctxt);
extern prtos_s32_t raise_pend_irqs(cpu_ctxt_t *ctxt);

struct cpu_user_regs *prtos_current_guest_regs_percpu[CONFIG_NO_CPUS];

extern prtos_s32_t do_hypercall(prtos_u32_t hc_nr, prtos_u64_t a1, prtos_u64_t a2,
                                prtos_u64_t a3, prtos_u64_t a4, prtos_u64_t a5);

extern int prtos_psci_handle(struct cpu_user_regs *regs);

static inline prtos_u64_t read_cntpct(void) {
    prtos_u64_t val;
    __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

/* ------------------------------------------------------------------ */
/* System register dispatch for hw-virt partitions                     */
/* ------------------------------------------------------------------ */
#define _HSR_SYSREG(op0,op1,crn,crm,op2) \
    (((op0)<<20)|((op2)<<17)|((op1)<<14)|((crn)<<10)|((crm)<<1))
#define _HSR_SYSREG_MASK \
    _HSR_SYSREG(0x3,0x7,0xf,0xf,0x7)

#define _HSR_ICC_SGI1R_EL1   _HSR_SYSREG(3,0,12,11,5)
#define _HSR_ICC_ASGI1R_EL1  _HSR_SYSREG(3,1,12,11,6)
#define _HSR_ICC_SGI0R_EL1   _HSR_SYSREG(3,2,12,11,7)
#define _HSR_ICC_SRE_EL1     _HSR_SYSREG(3,0,12,12,5)

static int prtos_sysreg_dispatch(struct cpu_user_regs *regs,
                                  prtos_u64_t hsr_bits) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_u32_t sysreg = (prtos_u32_t)hsr_bits & _HSR_SYSREG_MASK;
    int rt = (hsr_bits >> 5) & 0x1F;
    int is_read = hsr_bits & 1;

    switch (sysreg) {
    case _HSR_ICC_SGI1R_EL1:
    case _HSR_ICC_ASGI1R_EL1:
    case _HSR_ICC_SGI0R_EL1:
        if (!is_read && k->ctrl.g && k->ctrl.g->karch.vgic) {
            prtos_u64_t val = (rt == 31) ? 0 : regs->regs[rt];
            prtos_u32_t intid = (val >> 24) & 0xF;
            prtos_u16_t target_list = val & 0xFFFF;
            prtos_u32_t aff1 = (val >> 16) & 0xFF;
            int irm = (val >> 40) & 1;
            int vcpu_id = KID2VCPUID(k->ctrl.g->id);
            struct prtos_vgic_state *vgic = k->ctrl.g->karch.vgic;
            int i;
            prtos_u32_t ipi_mask = 0;  /* bitmask of pCPUs to notify */

            if (irm) {
                /* Broadcast: inject SGI to all vCPUs except sender */
                for (i = 0; i < (int)vgic->num_vcpus; i++)
                    if (i != vcpu_id) {
                        prtos_vgic_inject_sgi(vgic, i, intid);
                        int pcpu = vgic->vcpu_to_pcpu[i];
                        if (pcpu != (int)GET_CPU_ID() && pcpu < 32)
                            ipi_mask |= (1U << pcpu);
                    }
            } else {
                /* Targeted: TargetList bits are Aff0 values (physical
                 * CPU IDs since VMPIDR uses physical MPIDR).  Map each
                 * target physical CPU back to its vCPU index. */
                for (i = 0; i < 16; i++)
                    if (target_list & (1U << i)) {
                        int target_pcpu = (aff1 << 4) | i;
                        int v;
                        for (v = 0; v < (int)vgic->num_vcpus; v++) {
                            if (vgic->vcpu_to_pcpu[v] == (prtos_u8_t)target_pcpu) {
                                prtos_vgic_inject_sgi(vgic, v, intid);
                                if (target_pcpu != (int)GET_CPU_ID() && target_pcpu < 32)
                                    ipi_mask |= (1U << target_pcpu);
                                break;
                            }
                        }
                    }
            }

            /* Send hardware IPI to remote pCPUs so they flush the
             * pending virtual SGI into their ICH_LR. */
            for (i = 0; i < CONFIG_NO_CPUS && ipi_mask; i++) {
                if (ipi_mask & (1U << i)) {
                    aarch64_send_ipi_to(i);
                    ipi_mask &= ~(1U << i);
                }
            }
        }
        return 1;

    case _HSR_ICC_SRE_EL1:
        if (is_read) {
            prtos_u64_t val = 0x7;
            if (rt < 31) regs->regs[rt] = val;
        }
        return 1;

    default:
        if (is_read && rt < 31)
            regs->regs[rt] = 0;
        return 1;
    }
}

/*
 * aarch64_trap_handler - main C trap handler
 */
void aarch64_trap_handler(struct cpu_user_regs *regs, int from_guest) {
    prtos_u32_t esr = (prtos_u32_t)regs->esr;
    prtos_u32_t ec = (esr >> 26) & 0x3F;

    prtos_current_guest_regs_percpu[GET_CPU_ID()] = regs;

    /* Re-enable physical SPIs that were disabled during a previous trap's
     * interrupt forwarding.  The guest has had at least one execution
     * opportunity to process and EOI the virtual IRQ since then. */
    if (from_guest) {
        local_processor_t *spi_info = GET_LOCAL_PROCESSOR();
        kthread_t *spi_k = spi_info->sched.current_kthread;
        if (spi_k->ctrl.g && (spi_k->ctrl.g->karch.spi_fwd_mask[0] |
                               spi_k->ctrl.g->karch.spi_fwd_mask[1] |
                               spi_k->ctrl.g->karch.spi_fwd_mask[2] |
                               spi_k->ctrl.g->karch.spi_fwd_mask[3])) {
            int wi;
            for (wi = 0; wi < 4; wi++) {
                prtos_u64_t mask = spi_k->ctrl.g->karch.spi_fwd_mask[wi];
                spi_k->ctrl.g->karch.spi_fwd_mask[wi] = 0;
                int bit;
                for (bit = 0; bit < 64 && mask; bit++) {
                    if (mask & (1ULL << bit)) {
                        prtos_u32_t intid_re = (wi * 64 + bit) + 32;
                        volatile prtos_u32_t *gicd_isenabler =
                            (volatile prtos_u32_t *)(GIC_DIST_BASE + 0x100 + 4 * (intid_re / 32));
                        *gicd_isenabler = (1U << (intid_re % 32));
                        mask &= ~(1ULL << bit);
                    }
                }
            }
        }
    }

    if (from_guest == 2) {
        /* IRQ at EL2 (from hypervisor context) */
        prtos_u64_t iar;
        __asm__ __volatile__("mrs %0, S3_0_C12_C12_0" : "=r"(iar));
        prtos_u32_t intid = (prtos_u32_t)(iar & 0x3FF);

        if (intid < 1020) {
            if (intid == GIC_PPI_HYP_TIMER) {
                extern timer_handler_t aarch64_timer_handler;
                if (aarch64_timer_handler)
                    aarch64_timer_handler();
            }
            __asm__ __volatile__("msr S3_0_C12_C12_1, %0" : : "r"(iar));

            /* Physical SPI: inject into current partition's vGIC, then
             * disable the physical SPI at the GICD to prevent level-triggered
             * interrupt storm. Re-enabled in switch_kthread_arch_post. */
            if (intid >= 32) {
                local_processor_t *info = GET_LOCAL_PROCESSOR();
                kthread_t *k = info->sched.current_kthread;
                if (k->ctrl.g && k->ctrl.g->karch.vgic) {
                    prtos_vgic_inject_spi(k->ctrl.g->karch.vgic, intid);
                    prtos_vgic_flush_lrs_current();
                    /* Disable physical SPI to prevent re-fire */
                    volatile prtos_u32_t *gicd_icenabler =
                        (volatile prtos_u32_t *)(GIC_DIST_BASE + 0x180 + 4 * ((intid) / 32));
                    *gicd_icenabler = (1U << (intid % 32));
                    /* Mark SPI as needing re-enable after guest processes it */
                    k->ctrl.g->karch.spi_fwd_mask[(intid - 32) / 64] |= (1ULL << ((intid - 32) % 64));
                }
            }

            /* Guest virtual timer (PPI 27): mask timer output to prevent
             * level-triggered re-fire, then inject virtual IRQ to guest.
             * The guest's vClearTickInterrupt() will unmask and reload. */
            if (intid == 27) {
                /* Set CNTV_CTL_EL0.IMASK (bit 1) to suppress re-assertion */
                prtos_u64_t cntv_ctl;
                __asm__ __volatile__("mrs %0, cntv_ctl_el0" : "=r"(cntv_ctl));
                cntv_ctl |= (1ULL << 1);
                __asm__ __volatile__("msr cntv_ctl_el0, %0\n\tisb" : : "r"(cntv_ctl));

                local_processor_t *info = GET_LOCAL_PROCESSOR();
                kthread_t *k = info->sched.current_kthread;
                if (k->ctrl.g) {
                    if (k->ctrl.g->karch.vgic) {
                        int vcpu_id = KID2VCPUID(k->ctrl.g->id);
                        k->ctrl.g->karch.vgic->vcpu[vcpu_id].ppis[11].pending = 1;
                        extern void prtos_vgic_flush_lrs_current(void);
                        prtos_vgic_flush_lrs_current();
                    } else {
                        /* Para-virt: direct ICH_LR injection */
                        prtos_u64_t lr_val = (1ULL << 62) | (1ULL << 60) | (0xA0ULL << 48) | 27;
                        __asm__ __volatile__("msr S3_4_C12_C12_0, %0\n\tisb" : : "r"(lr_val));
                    }
                }
                /* If k->ctrl.g is NULL (idle task), timer is masked.
                 * Context switch post will detect ISTATUS and inject. */
            }

            cpu_ctxt_t ctxt;
            ctxt.irq_nr = (intid == GIC_SGI_IPI) ? 1 : 5;
            do_hyp_irq(&ctxt);
        }
        return;
    }

    if (!from_guest) {
        /* Exception from EL2 */
        return;
    }

    /* IRQ/FIQ from guest (from_guest==3) */
    if (from_guest == 3) {
        prtos_u64_t iar;
        __asm__ __volatile__("mrs %0, S3_0_C12_C12_0" : "=r"(iar));
        prtos_u32_t intid = (prtos_u32_t)(iar & 0x3FF);

        if (intid < 1020) {
            if (intid == GIC_PPI_HYP_TIMER) {
                extern timer_handler_t aarch64_timer_handler;
                if (aarch64_timer_handler)
                    aarch64_timer_handler();
            }
            __asm__ __volatile__("msr S3_0_C12_C12_1, %0" : : "r"(iar));

            /* IPI from another pCPU: flush pending vGIC IRQs (e.g. a
             * virtual SGI injected by a remote vCPU) into ICH_LR so
             * the guest receives it immediately. */
            if (intid == GIC_SGI_IPI) {
                local_processor_t *ipi_info = GET_LOCAL_PROCESSOR();
                kthread_t *ipi_k = ipi_info->sched.current_kthread;
                if (ipi_k->ctrl.g && ipi_k->ctrl.g->karch.vgic) {
                    extern void prtos_vgic_flush_lrs_current(void);
                    prtos_vgic_flush_lrs_current();
                }
            }

            /* Physical SPI: inject into vGIC and disable physical SPI */
            if (intid >= 32) {
                local_processor_t *info = GET_LOCAL_PROCESSOR();
                kthread_t *k = info->sched.current_kthread;
                if (k->ctrl.g && k->ctrl.g->karch.vgic) {
                    prtos_vgic_inject_spi(k->ctrl.g->karch.vgic, intid);
                    prtos_vgic_flush_lrs_current();
                    volatile prtos_u32_t *gicd_icenabler =
                        (volatile prtos_u32_t *)(GIC_DIST_BASE + 0x180 + 4 * ((intid) / 32));
                    *gicd_icenabler = (1U << (intid % 32));
                    k->ctrl.g->karch.spi_fwd_mask[(intid - 32) / 64] |= (1ULL << ((intid - 32) % 64));
                }
            }

            if (intid == 27) {
                prtos_u64_t cntv_ctl;
                __asm__ __volatile__("mrs %0, cntv_ctl_el0" : "=r"(cntv_ctl));
                cntv_ctl |= (1ULL << 1);
                __asm__ __volatile__("msr cntv_ctl_el0, %0\n\tisb" : : "r"(cntv_ctl));

                local_processor_t *info = GET_LOCAL_PROCESSOR();
                kthread_t *k = info->sched.current_kthread;
                if (k->ctrl.g) {
                    if (k->ctrl.g->karch.vgic) {
                        int vcpu_id = KID2VCPUID(k->ctrl.g->id);
                        k->ctrl.g->karch.vgic->vcpu[vcpu_id].ppis[11].pending = 1;
                        extern void prtos_vgic_flush_lrs_current(void);
                        prtos_vgic_flush_lrs_current();
                    } else {
                        prtos_u64_t lr_val = (1ULL << 62) | (1ULL << 60) | (0xA0ULL << 48) | 27;
                        __asm__ __volatile__("msr S3_4_C12_C12_0, %0\n\tisb" : : "r"(lr_val));
                    }
                }
            }

            cpu_ctxt_t ctxt;
            ctxt.irq_nr = (intid == GIC_SGI_IPI) ? 1 : 5;
            do_hyp_irq(&ctxt);
        }

        prtos_current_guest_regs_percpu[GET_CPU_ID()] = regs;
        {
            cpu_ctxt_t pend_ctxt;
            pend_ctxt.irq_nr = 0;
            raise_pend_irqs(&pend_ctxt);
        }
        return;
    }

    /* Synchronous exception from EL1 (guest), from_guest==1 */
    switch (ec) {
    case AARCH64_EC_HVC64: {
        prtos_u32_t hc_nr;
        prtos_s32_t result;

        hc_nr = (prtos_u32_t)regs->regs[0];

        if (hc_nr == NR_HYPERCALLS) {
            local_processor_t *info = GET_LOCAL_PROCESSOR();
            kthread_t *k = info->sched.current_kthread;
            if (k && k->ctrl.g) {
                partition_control_table_t *pct = k->ctrl.g->part_ctrl_table;
                if (pct->arch.irq_vector) {
                    regs->elr = pct->arch.irq_saved_pc;
                    regs->spsr = pct->arch.irq_saved_spsr;
                    regs->regs[0] = pct->arch.irq_saved_x0;
                    pct->arch.irq_vector = 0;
                }
            }
            break;
        }

        if (hc_nr == NR_HYPERCALLS + 1) {
            cpu_ctxt_t trap_ctxt;
            trap_ctxt.irq_nr = (prtos_u64_t)regs->regs[1];
            trap_ctxt.pc = regs->elr;
            trap_ctxt.sp = 0;
            do_hyp_trap(&trap_ctxt);
            break;
        }

        result = do_hypercall(hc_nr, regs->regs[1], regs->regs[2],
                              regs->regs[3], regs->regs[4], regs->regs[5]);
        regs->regs[0] = (prtos_u64_t)(prtos_s64_t)result;
        break;
    }
    case AARCH64_EC_SMC64: {
        regs->elr += 4;
        if (prtos_psci_handle(regs))
            break;
        regs->regs[0] = (prtos_u64_t)-1;
        break;
    }
    case AARCH64_EC_SYS64: {
        /* MSR/MRS trap from guest: dispatch to sysreg handler for hw-virt */
        local_processor_t *info = GET_LOCAL_PROCESSOR();
        kthread_t *k = info->sched.current_kthread;
        if (k->ctrl.g && k->ctrl.g->karch.vgic) {
            prtos_sysreg_dispatch(regs, (prtos_u64_t)esr);
        }
        regs->elr += 4;
        break;
    }
    case AARCH64_EC_IABT_LOW: {
        cpu_ctxt_t trap_ctxt;
        trap_ctxt.irq_nr = AARCH64_INSTR_PAGE_FAULT;
        trap_ctxt.pc = regs->elr;
        trap_ctxt.sp = regs->far;
        do_hyp_trap(&trap_ctxt);
        break;
    }
    case AARCH64_EC_DABT_LOW: {
        /* Data abort from guest: check if MMIO for hw-virt VGIC */
        local_processor_t *info = GET_LOCAL_PROCESSOR();
        kthread_t *k = info->sched.current_kthread;
        if (k->ctrl.g && k->ctrl.g->karch.vgic) {
            prtos_u64_t gpa;
            __asm__ __volatile__("mrs %0, hpfar_el2" : "=r"(gpa));
            gpa = (gpa >> 4) << 12;
            gpa |= (regs->far & 0xFFF);

            int is_write = (esr >> 6) & 1; /* WnR bit */
            int reg = (esr >> 16) & 0x1F;  /* SRT */
            int size = 1 << ((esr >> 22) & 0x3); /* SAS */

            if (prtos_mmio_dispatch(regs, gpa, is_write, reg, size) == 0) {
                regs->elr += 4;
                break;
            }
        }
        /* Unhandled: route to health monitor */
        cpu_ctxt_t trap_ctxt;
        trap_ctxt.irq_nr = AARCH64_LOAD_PAGE_FAULT;
        trap_ctxt.pc = regs->elr;
        trap_ctxt.sp = regs->far;
        do_hyp_trap(&trap_ctxt);
        break;
    }
    default:
        kprintf("[TRAP] UNHANDLED EC=0x%x ESR=0x%x ELR=0x%llx FAR=0x%llx\n",
                ec, esr, regs->elr, regs->far);
        halt_system();
        break;
    }

    prtos_current_guest_regs_percpu[GET_CPU_ID()] = regs;
    {
        cpu_ctxt_t pend_ctxt;
        pend_ctxt.irq_nr = 0;
        raise_pend_irqs(&pend_ctxt);
    }
}

/*
 * do_hypercall - dispatch a hypercall from the table
 */
prtos_s32_t do_hypercall(prtos_u32_t hc_nr, prtos_u64_t a1, prtos_u64_t a2,
                         prtos_u64_t a3, prtos_u64_t a4, prtos_u64_t a5) {
    extern prtos_address_t hypercalls_table[];
    extern prtos_u32_t hypercall_flags_table[];
    typedef prtos_s32_t (*hcall_0_t)(void);
    typedef prtos_s32_t (*hcall_1_t)(prtos_u64_t);
    typedef prtos_s32_t (*hcall_2_t)(prtos_u64_t, prtos_u64_t);
    typedef prtos_s32_t (*hcall_3_t)(prtos_u64_t, prtos_u64_t, prtos_u64_t);
    typedef prtos_s32_t (*hcall_4_t)(prtos_u64_t, prtos_u64_t, prtos_u64_t, prtos_u64_t);
    typedef prtos_s32_t (*hcall_5_t)(prtos_u64_t, prtos_u64_t, prtos_u64_t, prtos_u64_t, prtos_u64_t);

    if (hc_nr >= NR_HYPERCALLS) return -1;

    prtos_address_t handler = hypercalls_table[hc_nr];
    if (!handler) return -1;

    prtos_u32_t flags = hypercall_flags_table[hc_nr];
    prtos_u32_t nargs = flags & 0xF;

    switch (nargs) {
    case 0: return ((hcall_0_t)handler)();
    case 1: return ((hcall_1_t)handler)(a1);
    case 2: return ((hcall_2_t)handler)(a1, a2);
    case 3: return ((hcall_3_t)handler)(a1, a2, a3);
    case 4: return ((hcall_4_t)handler)(a1, a2, a3, a4);
    case 5: return ((hcall_5_t)handler)(a1, a2, a3, a4, a5);
    default: return -1;
    }
}
