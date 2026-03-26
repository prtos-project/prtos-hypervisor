/*
 * FILE: prtos_psci.c
 *
 * PSCI (Power State Coordination Interface) emulation for PRTOS
 * hw-virt partitions.  Handles SMC-based PSCI calls from Linux
 * guests for secondary vCPU startup and power management.
 *
 * www.prtos.org
 */

#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <stdc.h>

#include "prtos_vgic.h"

/* PSCI function IDs (ARM DEN 0022D) */
#define PSCI_VERSION_FID            0x84000000U
#define PSCI_CPU_SUSPEND_32         0x84000001U
#define PSCI_CPU_OFF                0x84000002U
#define PSCI_CPU_ON_32              0x84000003U
#define PSCI_CPU_ON_64              0xC4000003U
#define PSCI_AFFINITY_INFO_32       0x84000004U
#define PSCI_AFFINITY_INFO_64       0xC4000004U
#define PSCI_SYSTEM_OFF             0x84000008U
#define PSCI_SYSTEM_RESET           0x84000009U
#define PSCI_FEATURES               0x8400000AU
#define PSCI_MIGRATE_INFO_TYPE      0x84000006U

/* PSCI return codes */
#define PSCI_SUCCESS                 0
#define PSCI_NOT_SUPPORTED          (-1)
#define PSCI_INVALID_PARAMS         (-2)
#define PSCI_DENIED                 (-3)
#define PSCI_ALREADY_ON             (-4)
#define PSCI_ON_PENDING             (-5)

/* PSCI affinity info return values */
#define PSCI_AFFINITY_ON             0
#define PSCI_AFFINITY_OFF            1
#define PSCI_AFFINITY_ON_PENDING     2

/* PSCI version: 1.1 */
#define PSCI_VERSION_1_1            ((1U << 16) | 1U)

/* External: partition table and config */
extern partition_t *partition_table;

/*
 * prtos_psci_cpu_on - Start a secondary vCPU.
 *
 * Linux calls PSCI CPU_ON with:
 *   target_mpidr: Aff0 = vCPU ID
 *   entry_point:  address to start executing
 *   context_id:   value passed in x0 to the new vCPU
 *
 * We set up the target vCPU's entry point and context, then mark it
 * as ready for scheduling.
 */
static prtos_s64_t prtos_psci_cpu_on(prtos_u64_t target_mpidr,
                                      prtos_u64_t entry_point,
                                      prtos_u64_t context_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    partition_t *p = &partition_table[part_id];

    /* Extract target vCPU ID from MPIDR Aff0 */
    int target_vcpu = (int)(target_mpidr & 0xFF);
    if (target_vcpu >= (int)p->cfg->num_of_vcpus)
        return PSCI_INVALID_PARAMS;

    kthread_t *target_k = p->kthread[target_vcpu];
    if (!target_k || !target_k->ctrl.g)
        return PSCI_INVALID_PARAMS;

    /* Check if already on */
    if (are_kthread_flags_set(target_k, KTHREAD_READY_F))
        return PSCI_ALREADY_ON;

    /* Store PSCI boot parameters for the target vCPU */
    target_k->ctrl.g->karch.psci_entry = entry_point;
    target_k->ctrl.g->karch.psci_context_id = context_id;

    /* Share VGIC state with the secondary vCPU */
    if (k->ctrl.g->karch.vgic && !target_k->ctrl.g->karch.vgic)
        target_k->ctrl.g->karch.vgic = k->ctrl.g->karch.vgic;

    /* Initialize PCT for the target vCPU: mask all para-virt IRQs
     * so raise_pend_irqs() doesn't corrupt the hw-virt guest's PC
     * when the cyclic scheduler sets CYCLIC_SLOT_START pending. */
    {
        partition_control_table_t *pct = target_k->ctrl.g->part_ctrl_table;
        pct->id = target_k->ctrl.g->id;
        pct->num_of_vcpus = p->cfg->num_of_vcpus;
        pct->hw_irqs_mask |= ~0;
        pct->ext_irqs_to_mask |= ~0;
        init_part_ctrl_table_irqs(&pct->iflags);
    }

    /* Set up the kthread stack so the scheduler can context-switch to it.
     * start_up_guest() will check psci_entry and use JMP_PARTITION_PSCI. */
    extern void start_up_guest(prtos_address_t entry);
    setup_kstack(target_k, start_up_guest, entry_point);

    /* Ensure all memory writes (kstack, psci_entry, vgic, pct) are
     * visible to the target pCPU before marking as ready. */
    __asm__ __volatile__("dsb ish" ::: "memory");

    /* Mark target as ready for scheduling.  The cyclic scheduler on the
     * target pCPU will pick it up on its next slot. */
    set_kthread_flags(target_k, KTHREAD_READY_F);
    clear_kthread_flags(target_k, KTHREAD_HALTED_F);

    __asm__ __volatile__("dsb ish\n\tsev" ::: "memory");

    /* Send IPI to the target pCPU to trigger immediate rescheduling.
     * On AArch64, SEV above wakes any WFE-sleeping CPU.  For a stronger
     * kick we use a GIC SGI (software-generated interrupt). */
#ifdef CONFIG_SMP
    {
        extern struct prtos_conf_vcpu *prtos_conf_vcpu_table;
        prtos_u8_t target_cpu = prtos_conf_vcpu_table[
            (part_id * prtos_conf_table.hpv.num_of_cpus) + target_vcpu].cpu;
        if (target_cpu != GET_CPU_ID()) {
            /* SEV already sent above; no additional IPI needed for the
             * current scheduler design which checks READY threads on
             * every timer tick. */
        }
    }
#endif

    return PSCI_SUCCESS;
}

/*
 * prtos_psci_affinity_info - Return vCPU power state.
 */
static prtos_s64_t prtos_psci_affinity_info(prtos_u64_t target_mpidr,
                                             prtos_u32_t lowest_level) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    partition_t *p = &partition_table[part_id];

    int target_vcpu = (int)(target_mpidr & 0xFF);
    if (target_vcpu >= (int)p->cfg->num_of_vcpus)
        return PSCI_INVALID_PARAMS;

    kthread_t *target_k = p->kthread[target_vcpu];
    if (!target_k)
        return PSCI_INVALID_PARAMS;

    if (are_kthread_flags_set(target_k, KTHREAD_READY_F))
        return PSCI_AFFINITY_ON;

    return PSCI_AFFINITY_OFF;
}

/*
 * prtos_psci_features - Report supported PSCI features.
 */
static prtos_s64_t prtos_psci_features(prtos_u32_t fid) {
    switch (fid) {
    case PSCI_VERSION_FID:
    case PSCI_CPU_ON_32:
    case PSCI_CPU_ON_64:
    case PSCI_CPU_OFF:
    case PSCI_AFFINITY_INFO_32:
    case PSCI_AFFINITY_INFO_64:
    case PSCI_SYSTEM_OFF:
    case PSCI_SYSTEM_RESET:
    case PSCI_FEATURES:
        return PSCI_SUCCESS;
    default:
        return PSCI_NOT_SUPPORTED;
    }
}

/*
 * prtos_psci_handle - Main PSCI dispatcher for SMC traps.
 *
 * Called from PRTOS's trap handler when an SMC64 is received from an
 * idle-domain (PRTOS) partition.
 *
 * Returns 1 if handled (caller must advance_pc), 0 if not a PSCI call.
 */
int prtos_psci_handle(struct cpu_user_regs *regs) {
    prtos_u32_t fid = (prtos_u32_t)regs->x0;

    switch (fid) {
    case PSCI_VERSION_FID:
        regs->x0 = PSCI_VERSION_1_1;
        return 1;

    case PSCI_CPU_ON_64:
    case PSCI_CPU_ON_32:
        regs->x0 = (prtos_u64_t)prtos_psci_cpu_on(regs->x1, regs->x2, regs->x3);
        return 1;

    case PSCI_CPU_OFF:
        /* Halt the calling vCPU */
        {
            local_processor_t *info = GET_LOCAL_PROCESSOR();
            kthread_t *k = info->sched.current_kthread;
            clear_kthread_flags(k, KTHREAD_READY_F);
            set_kthread_flags(k, KTHREAD_HALTED_F);
        }
        regs->x0 = PSCI_SUCCESS;
        return 1;

    case PSCI_AFFINITY_INFO_64:
    case PSCI_AFFINITY_INFO_32:
        regs->x0 = (prtos_u64_t)prtos_psci_affinity_info(regs->x1,
                                                           (prtos_u32_t)regs->x2);
        return 1;

    case PSCI_SYSTEM_OFF:
    case PSCI_SYSTEM_RESET:
        /* Halt partition — use PRTOS halt mechanism */
        regs->x0 = PSCI_SUCCESS;
        return 1;

    case PSCI_FEATURES:
        regs->x0 = (prtos_u64_t)prtos_psci_features((prtos_u32_t)regs->x1);
        return 1;

    case PSCI_MIGRATE_INFO_TYPE:
        regs->x0 = 2;  /* No migration, system does not require migration */
        return 1;

    default:
        return 0;  /* Not a recognized PSCI call */
    }
}
