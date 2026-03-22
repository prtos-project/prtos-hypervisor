/*
 * FILE: traps.c
 *
 * AArch64 PRTOS hypercall and IRQ dispatch hooks
 *
 * www.prtos.org
 */

#include <assert.h>
#include <irqs.h>
#include <kthread.h>
#include <sched.h>
#include <stdc.h>
#include <processor.h>
#include <hypercalls.h>

#include <arch/processor.h>

#include "prtos_vgic.h"

/* Defined in the linker script: hypercalls_table[NR_HYPERCALLS] */
extern prtos_s32_t (*hypercalls_table[])(prtos_word_t, ...);

/* Declared in irqs.c */
extern void do_hyp_irq(cpu_ctxt_t *ctxt);
extern void do_hyp_trap(cpu_ctxt_t *ctxt);

/*
 * PRTOS IRET hypercall number: use NR_HYPERCALLS as sentinel.
 * Partition calls HVC #0 with x0 = PRTOS_IRET_NR after handling a virtual IRQ.
 */
#define PRTOS_IRET_NR NR_HYPERCALLS

/*
 * PRTOS_RAISE_TRAP_NR - Partition raises a trap event to the hypervisor.
 * On AArch64, EL1 exceptions don't trap to EL2, so partitions use this
 * para-virtualized hypercall to notify the hypervisor of a fault.
 * x0 = PRTOS_RAISE_TRAP_NR, x1 = trap number (maps to HM event via
 * trap_nr + PRTOS_HM_MAX_GENERIC_EVENTS).
 */
#define PRTOS_RAISE_TRAP_NR (NR_HYPERCALLS + 1)

/*
 * prtos_do_iret - Restore guest PC after virtual IRQ handling.
 *
 * Called when the partition's trap dispatch stub completes.
 * Restores original PC and SPSR from pct_arch, clears irq_vector.
 */
static int prtos_do_iret(struct cpu_user_regs *regs) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    partition_control_table_t *pct;

    if (!info->sched.current_kthread->ctrl.g) return 0;
    pct = info->sched.current_kthread->ctrl.g->part_ctrl_table;
    if (!pct->arch.irq_vector) return 0;

    regs->pc = pct->arch.irq_saved_pc;
    regs->cpsr = pct->arch.irq_saved_spsr;
    regs->x0 = pct->arch.irq_saved_x0;
    pct->arch.irq_vector = 0;
    return 1;
}

/*
 * prtos_do_hvc - PRTOS AArch64 hypercall dispatch
 *
 * Called from Xen's do_trap_guest_sync when HSR_EC_HVC64 with ISS == 0.
 * Reads x0 (hypercall number) and x1-x5 (arguments) from the guest regs,
 * dispatches to hypercalls_table, stores result back in x0.
 *
 * Returns 1 if handled, 0 if not a PRTOS hypercall (number out of range).
 */
int prtos_do_hvc(struct cpu_user_regs *regs) {
    prtos_u64_t nr = regs->x0;

    /* IRET: partition finished handling a virtual IRQ */
    if (nr == PRTOS_IRET_NR) {
        prtos_do_iret(regs);
        return 1;
    }

    /* Raise trap: partition notifies hypervisor of a fault (para-virt) */
    if (nr == PRTOS_RAISE_TRAP_NR) {
        cpu_ctxt_t ctxt;
        memset(&ctxt, 0, sizeof(ctxt));
        ctxt.irq_nr = (prtos_u64_t)regs->x1;
        ctxt.pc = regs->pc;
        ctxt.sp = regs->cpsr;
        do_hyp_trap(&ctxt);
        return 1;
    }

    if (nr >= NR_HYPERCALLS) return 0;

    if (!hypercalls_table[nr]) return 0;

    regs->x0 = (prtos_u64_t)hypercalls_table[nr]((prtos_word_t)regs->x1, (prtos_word_t)regs->x2, (prtos_word_t)regs->x3, (prtos_word_t)regs->x4,
                                                 (prtos_word_t)regs->x5);

    return 1;
}

/*
 * prtos_timer_irq_dispatch - Bridge Xen timer IRQ to PRTOS
 *
 * Called from static_htimer_isr with the GIC IRQ number.
 * For hw-virt partitions (Linux): inject timer as PPI 27 via VGIC ICH_LR.
 * For para-virt partitions (FreeRTOS): use existing PCT-based delivery.
 */
void prtos_timer_irq_dispatch(int irq_nr) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;

    /* For hw-virt partitions, inject virtual timer (PPI 27) via VGIC
     * and drive the PRTOS cyclic scheduler.  We cannot use do_hyp_irq()
     * because the PRTOS IRQ handler table has no handler for the GICv3
     * timer IRQ.  Instead, replicate the do_hyp_irq scheduler pattern:
     * set SCHED_PENDING, then poll-and-schedule after the IRQ completes. */
    /* For hw-virt partitions, inject virtual timer (PPI 27) via VGIC
     * and trigger the scheduler via do_hyp_irq with the registered
     * aarch64_htimer_irq_handler (set_sched_pending). */
    if (k->ctrl.g && k->ctrl.g->karch.vgic) {
        int vcpu_id = KID2VCPUID(k->ctrl.g->id);
        struct prtos_vgic_state *vgic = k->ctrl.g->karch.vgic;
        vgic->vcpu[vcpu_id].ppis[11].pending = 1;
    }

    /* All paths (para-virt, hw-virt, idle) use do_hyp_irq for the
     * scheduler.  The registered aarch64_htimer_irq_handler calls
     * set_sched_pending(), and do_hyp_irq's post-handler loop checks
     * SCHED_PENDING and calls schedule(). */
    cpu_ctxt_t ctxt;
    memset(&ctxt, 0, sizeof(ctxt));
    ctxt.irq_nr = (prtos_u64_t)irq_nr;
    do_hyp_irq(&ctxt);
}

/*
 * prtos_stage2_fault_dispatch - Handle stage-2 MMU faults for PRTOS partitions.
 *
 * Called from Xen's do_trap_stage2_abort_guest when the faulting domain
 * is the idle domain (i.e. a PRTOS partition).  Routes the fault through
 * PRTOS's Health Monitor (do_hyp_trap) so that the configured HM action
 * (halt, propagate, etc.) is applied.
 */
void prtos_stage2_fault_dispatch(prtos_u64_t pc, prtos_u64_t cpsr, int is_data) {
    cpu_ctxt_t ctxt;
    memset(&ctxt, 0, sizeof(ctxt));
    ctxt.pc = pc;
    ctxt.sp = cpsr;
    if (is_data)
        ctxt.irq_nr = AARCH64_DATA_ABORT;
    else
        ctxt.irq_nr = AARCH64_PREFETCH_ABORT;
    do_hyp_trap(&ctxt);
}

/*
 * prtos_sysreg_dispatch - Handle trapped system register accesses for
 * hw-virt (idle-domain) partitions.
 *
 * Xen's do_sysreg -> vgic_emulate path dereferences domain->arch.vgic
 * which is NULL for idle domain.  This function intercepts GICv3 ICC_*
 * system register traps and handles them via PRTOS's VGIC infrastructure.
 *
 * Returns 1 if handled (caller should advance_pc), 0 if unhandled.
 */

/* HSR_SYSREG encoding helpers (from Xen arm64/hsr.h) */
#define _HSR_SYSREG(op0,op1,crn,crm,op2) \
    (((op0)<<20)|((op2)<<17)|((op1)<<14)|((crn)<<10)|((crm)<<1))
#define _HSR_SYSREG_MASK \
    _HSR_SYSREG(0x3,0x7,0xf,0xf,0x7)

/* ICC_SGI1R_EL1 = S3_0_C12_C11_5 */
#define _HSR_ICC_SGI1R_EL1   _HSR_SYSREG(3,0,12,11,5)
/* ICC_ASGI1R_EL1 = S3_1_C12_C11_6 */
#define _HSR_ICC_ASGI1R_EL1  _HSR_SYSREG(3,1,12,11,6)
/* ICC_SGI0R_EL1 = S3_2_C12_C11_7 */
#define _HSR_ICC_SGI0R_EL1   _HSR_SYSREG(3,2,12,11,7)
/* ICC_SRE_EL1 = S3_0_C12_C12_5 */
#define _HSR_ICC_SRE_EL1     _HSR_SYSREG(3,0,12,12,5)

int prtos_sysreg_dispatch(struct cpu_user_regs *regs,
                           prtos_u64_t hsr_bits) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_u32_t sysreg = (prtos_u32_t)hsr_bits & _HSR_SYSREG_MASK;
    int rt = (hsr_bits >> 5) & 0x1F;     /* bits [9:5] = Rt */
    int is_read = hsr_bits & 1;           /* bit 0 = direction (1=read) */

    switch (sysreg) {
    case _HSR_ICC_SGI1R_EL1:
    case _HSR_ICC_ASGI1R_EL1:
    case _HSR_ICC_SGI0R_EL1:
        if (!is_read && k->ctrl.g && k->ctrl.g->karch.vgic) {
            /* Decode SGI write: extract target list and INTID */
            prtos_u64_t val = (rt == 31) ? 0 : (&regs->x0)[rt];
            prtos_u32_t intid = (val >> 24) & 0xF;
            prtos_u16_t target_list = val & 0xFFFF;
            prtos_u32_t aff1 = (val >> 16) & 0xFF;
            int irm = (val >> 40) & 1;
            int vcpu_id = KID2VCPUID(k->ctrl.g->id);
            struct prtos_vgic_state *vgic = k->ctrl.g->karch.vgic;
            int i;

            if (irm) {
                /* IRM=1: send to all PEs except self */
                for (i = 0; i < (int)vgic->num_vcpus; i++)
                    if (i != vcpu_id)
                        prtos_vgic_inject_sgi(vgic, i, intid);
            } else {
                /* IRM=0: send to target list (Aff1.TargetList) */
                for (i = 0; i < 16 && i < (int)vgic->num_vcpus; i++)
                    if (target_list & (1U << i))
                        prtos_vgic_inject_sgi(vgic, (aff1 << 4) | i, intid);
            }
        }
        return 1;

    case _HSR_ICC_SRE_EL1:
        if (is_read) {
            /* Report SRE=1 (system register interface enabled) */
            prtos_u64_t val = 0x7;  /* SRE | DIB | DFB */
            if (rt < 31) (&regs->x0)[rt] = val;
        }
        /* Write to SRE: ignore (already enabled) */
        return 1;

    default:
        /* Unhandled sysreg for idle domain: treat as RAZ/WI */
        if (is_read && rt < 31)
            (&regs->x0)[rt] = 0;
        return 1;
    }
}

/* Declared in irqs.c */
extern prtos_s32_t raise_pend_irqs(cpu_ctxt_t *ctxt);

/*
 * prtos_raise_pend_irqs_aarch64 - Check and deliver pending virtual IRQs.
 *
 * Called from leave_hypervisor_to_guest() before returning to partition.
 * If a virtual IRQ is pending, redirects the guest PC to the trap dispatch stub.
 */
void prtos_raise_pend_irqs_aarch64(void) {
    cpu_ctxt_t ctxt;
    memset(&ctxt, 0, sizeof(ctxt));
    raise_pend_irqs(&ctxt);
}
