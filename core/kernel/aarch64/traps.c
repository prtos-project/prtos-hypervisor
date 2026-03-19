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

/* Defined in the linker script: hypercalls_table[NR_HYPERCALLS] */
extern prtos_s32_t (*hypercalls_table[])(prtos_word_t, ...);

/* Declared in irqs.c */
extern void do_hyp_irq(cpu_ctxt_t *ctxt);

/*
 * PRTOS IRET hypercall number: use NR_HYPERCALLS as sentinel.
 * Partition calls HVC #0 with x0 = PRTOS_IRET_NR after handling a virtual IRQ.
 */
#define PRTOS_IRET_NR NR_HYPERCALLS

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

    if (nr >= NR_HYPERCALLS) return 0;

    if (!hypercalls_table[nr]) return 0;

    regs->x0 = (prtos_u64_t)hypercalls_table[nr]((prtos_word_t)regs->x1, (prtos_word_t)regs->x2, (prtos_word_t)regs->x3, (prtos_word_t)regs->x4,
                                                 (prtos_word_t)regs->x5);

    return 1;
}

/*
 * prtos_timer_irq_dispatch - Bridge Xen timer IRQ to PRTOS do_hyp_irq
 *
 * Called from static_htimer_isr with the GIC IRQ number.
 */
void prtos_timer_irq_dispatch(int irq_nr) {
    cpu_ctxt_t ctxt;
    memset(&ctxt, 0, sizeof(ctxt));
    ctxt.irq_nr = (prtos_u64_t)irq_nr;
    do_hyp_irq(&ctxt);
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
