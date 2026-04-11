/*
 * FILE: psci.c
 *
 * PSCI (Power State Coordination Interface) emulation for PRTOS hw-virt
 * partitions on AArch64.  Handles SMC-based PSCI calls from Linux guests
 * for secondary vCPU startup (CPU_ON), CPU_OFF, and VERSION probing.
 *
 * http://www.prtos.org/
 */

#include <irqs.h>
#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <stdc.h>

/* PSCI function IDs (SMC64) */
#define PSCI_VERSION             0x84000000
#define PSCI_CPU_SUSPEND_32      0x84000001
#define PSCI_CPU_OFF             0x84000002
#define PSCI_CPU_ON_32           0x84000003
#define PSCI_CPU_ON_64           0xC4000003
#define PSCI_AFFINITY_INFO_64    0xC4000004
#define PSCI_SYSTEM_OFF          0x84000008
#define PSCI_SYSTEM_RESET        0x84000009
#define PSCI_FEATURES            0x8400000A
#define PSCI_CPU_FREEZE          0x8400000B

/* PSCI return values */
#define PSCI_SUCCESS              0
#define PSCI_NOT_SUPPORTED       -1
#define PSCI_INVALID_PARAMETERS  -2
#define PSCI_DENIED              -3
#define PSCI_ALREADY_ON          -4
#define PSCI_ON_PENDING          -5
#define PSCI_INTERNAL_FAILURE    -6

/* External: partition table */
extern partition_t *partition_table;
extern struct prtos_conf_vcpu *prtos_conf_vcpu_table;
extern const struct prtos_conf prtos_conf_table;

/* Map a target MPIDR (physical CPU affinity) to vCPU index within a partition.
 * VMPIDR_EL2 uses physical CPU IDs so that ICC_SGI1_EL1 routing works.
 * When the guest calls PSCI CPU_ON with a target MPIDR matching the DTS reg
 * value (= physical CPU), we must resolve which vCPU index that corresponds to. */
static int mpidr_to_vcpu(prtos_s32_t part_id, prtos_u32_t num_vcpus,
                          prtos_u64_t target_mpidr) {
    int target_pcpu = (int)(target_mpidr & 0xFF);
    int i;
    for (i = 0; i < (int)num_vcpus; i++) {
        prtos_u8_t cpu = prtos_conf_vcpu_table[
            (part_id * prtos_conf_table.hpv.num_of_cpus) + i].cpu;
        if (cpu == target_pcpu)
            return i;
    }
    return -1;
}

/*
 * prtos_psci_cpu_on - Start a secondary vCPU.
 */
static long prtos_psci_cpu_on(prtos_u64_t target_mpidr,
                               prtos_u64_t entry_point,
                               prtos_u64_t context_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    partition_t *p = &partition_table[part_id];

    int target_vcpu = mpidr_to_vcpu(part_id, p->cfg->num_of_vcpus, target_mpidr);
    if (target_vcpu < 0)
        return PSCI_INVALID_PARAMETERS;

    kthread_t *target_k = p->kthread[target_vcpu];
    if (!target_k || !target_k->ctrl.g)
        return PSCI_INVALID_PARAMETERS;

    if (are_kthread_flags_set(target_k, KTHREAD_READY_F))
        return PSCI_ALREADY_ON;

    /* Store PSCI boot parameters for the target vCPU */
    target_k->ctrl.g->karch.psci_entry = entry_point;
    target_k->ctrl.g->karch.psci_context_id = context_id;

    /* Initialize PCT for the target vCPU */
    {
        partition_control_table_t *pct = target_k->ctrl.g->part_ctrl_table;
        pct->id = target_k->ctrl.g->id;
        pct->num_of_vcpus = p->cfg->num_of_vcpus;
        pct->hw_irqs_mask |= ~0;
        pct->ext_irqs_to_mask |= ~0;
        init_part_ctrl_table_irqs(&pct->iflags);
    }

    /* Set up the kthread stack */
    extern void start_up_guest(prtos_address_t entry);
    setup_kstack(target_k, start_up_guest, entry_point);

    /* Memory barrier before making visible to other CPUs */
    __asm__ __volatile__("dmb ish" ::: "memory");

    /* Mark target as ready for scheduling */
    set_kthread_flags(target_k, KTHREAD_READY_F);
    clear_kthread_flags(target_k, KTHREAD_HALTED_F);

    __asm__ __volatile__("dmb ish" ::: "memory");

    return PSCI_SUCCESS;
}

/*
 * prtos_psci_affinity_info - Return vCPU state.
 */
static long prtos_psci_affinity_info(prtos_u64_t target_mpidr,
                                      prtos_u64_t lowest_affinity_level) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    partition_t *p = &partition_table[part_id];

    int target_vcpu = mpidr_to_vcpu(part_id, p->cfg->num_of_vcpus, target_mpidr);
    if (target_vcpu < 0)
        return PSCI_INVALID_PARAMETERS;

    kthread_t *target_k = p->kthread[target_vcpu];
    if (!target_k)
        return PSCI_INVALID_PARAMETERS;

    if (are_kthread_flags_set(target_k, KTHREAD_READY_F))
        return 0;  /* ON */

    return 1;  /* OFF */
}

/*
 * prtos_psci_handle - Main PSCI dispatcher for SMC from guest.
 *
 * Returns 1 if handled, 0 if not a PSCI call.
 */
int prtos_psci_handle(struct cpu_user_regs *regs) {
    prtos_u64_t func_id = regs->regs[0];
    long ret;

    switch (func_id) {
    case PSCI_VERSION:
        /* PSCI version 1.0 */
        regs->regs[0] = (1UL << 16) | 0;
        return 1;

    case PSCI_CPU_ON_32:
    case PSCI_CPU_ON_64:
        ret = prtos_psci_cpu_on(regs->regs[1], regs->regs[2], regs->regs[3]);
        regs->regs[0] = (prtos_u64_t)ret;
        return 1;

    case PSCI_CPU_OFF:
        /* Halt the current vCPU */
        {
            local_processor_t *info = GET_LOCAL_PROCESSOR();
            kthread_t *k = info->sched.current_kthread;
            if (k) {
                set_kthread_flags(k, KTHREAD_HALTED_F);
                clear_kthread_flags(k, KTHREAD_READY_F);
                set_sched_pending();
            }
        }
        regs->regs[0] = PSCI_SUCCESS;
        return 1;

    case PSCI_AFFINITY_INFO_64:
        ret = prtos_psci_affinity_info(regs->regs[1], regs->regs[2]);
        regs->regs[0] = (prtos_u64_t)ret;
        return 1;

    case PSCI_FEATURES:
        /* Report supported functions */
        switch (regs->regs[1]) {
        case PSCI_VERSION:
        case PSCI_CPU_ON_32:
        case PSCI_CPU_ON_64:
        case PSCI_CPU_OFF:
        case PSCI_AFFINITY_INFO_64:
        case PSCI_SYSTEM_OFF:
        case PSCI_SYSTEM_RESET:
            regs->regs[0] = PSCI_SUCCESS;
            break;
        default:
            regs->regs[0] = (prtos_u64_t)PSCI_NOT_SUPPORTED;
            break;
        }
        return 1;

    case PSCI_SYSTEM_OFF:
    case PSCI_SYSTEM_RESET:
        halt_system();
        return 1;

    default:
        return 0;  /* Not a PSCI call we handle */
    }
}
