/*
 * FILE: prtos_iocsr.c
 *
 * IOCSR emulation for LoongArch 64-bit hw-virt partitions.
 * Handles console output, timer, IPI, and HSM-like vCPU management
 * via syscall-based interface (equivalent to RISC-V SBI emulation).
 *
 * http://www.prtos.org/
 */

#include <irqs.h>
#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <stdc.h>

/* IOCSR emulation extension IDs (in a7) */
#define PRTOS_EXT_CONSOLE  0x10
#define PRTOS_EXT_TIMER    0x20
#define PRTOS_EXT_IPI      0x30
#define PRTOS_EXT_HSM      0x40
#define PRTOS_EXT_BASE     0x50

/* HSM function IDs */
#define HSM_VCPU_START       0
#define HSM_VCPU_STOP        1
#define HSM_VCPU_GET_STATUS  2

/* HSM states */
#define HSM_STATE_STARTED  0
#define HSM_STATE_STOPPED  1

/* Return codes */
#define RET_SUCCESS           0
#define RET_ERR_NOT_SUPPORTED -2
#define RET_ERR_INVALID_PARAM -3
#define RET_ERR_ALREADY       -6

extern partition_t *partition_table;
extern struct prtos_conf_vcpu *prtos_conf_vcpu_table;

/* Console putchar via UART MMIO */
static void iocsr_console_putchar(int ch) {
    /* LoongArch QEMU virt UART at 0x1FE001E0 (uncached DMW) */
    volatile prtos_u8_t *uart = (volatile prtos_u8_t *)0x80001FE001E0ULL;
    while (!(uart[5] & 0x20))
        ;
    uart[0] = (prtos_u8_t)ch;
}

/*
 * HSM vCPU start - Start a secondary vCPU.
 */
static long prtos_hsm_vcpu_start(prtos_u64_t target_vcpuid,
                                  prtos_u64_t start_addr,
                                  prtos_u64_t opaque) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    partition_t *p = &partition_table[part_id];

    int target_vcpu = (int)target_vcpuid;
    if (target_vcpu >= (int)p->cfg->num_of_vcpus)
        return RET_ERR_INVALID_PARAM;

    kthread_t *target_k = p->kthread[target_vcpu];
    if (!target_k || !target_k->ctrl.g)
        return RET_ERR_INVALID_PARAM;

    if (are_kthread_flags_set(target_k, KTHREAD_READY_F))
        return RET_ERR_ALREADY;

    /* Store boot parameters */
    target_k->ctrl.g->karch.hsm_entry = start_addr;
    target_k->ctrl.g->karch.hsm_opaque = opaque;

    /* Initialize PCT for target vCPU */
    {
        partition_control_table_t *pct = target_k->ctrl.g->part_ctrl_table;
        pct->id = target_k->ctrl.g->id;
        pct->num_of_vcpus = p->cfg->num_of_vcpus;
        pct->hw_irqs_mask |= ~0;
        pct->ext_irqs_to_mask |= ~0;
        init_part_ctrl_table_irqs(&pct->iflags);
    }

    /* Set up kthread stack so scheduler can context-switch to it */
    extern void start_up_guest(prtos_address_t entry);
    setup_kstack(target_k, start_up_guest, start_addr);

    __asm__ __volatile__("dbar 0" ::: "memory");

    set_kthread_flags(target_k, KTHREAD_READY_F);
    clear_kthread_flags(target_k, KTHREAD_HALTED_F);

    __asm__ __volatile__("dbar 0" ::: "memory");

    return RET_SUCCESS;
}

static long prtos_hsm_vcpu_get_status(prtos_u64_t target_vcpuid) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    partition_t *p = &partition_table[part_id];

    int target_vcpu = (int)target_vcpuid;
    if (target_vcpu >= (int)p->cfg->num_of_vcpus)
        return RET_ERR_INVALID_PARAM;

    kthread_t *target_k = p->kthread[target_vcpu];
    if (!target_k)
        return RET_ERR_INVALID_PARAM;

    if (are_kthread_flags_set(target_k, KTHREAD_READY_F))
        return HSM_STATE_STARTED;

    return HSM_STATE_STOPPED;
}

static long prtos_iocsr_send_ipi(prtos_u64_t vcpu_mask, prtos_u64_t vcpu_mask_base) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    partition_t *p = &partition_table[part_id];
    int i;

    for (i = 0; i < (int)p->cfg->num_of_vcpus; i++) {
        int vcpuid = (int)vcpu_mask_base + i;
        if (vcpuid >= (int)p->cfg->num_of_vcpus) break;
        if (!(vcpu_mask & (1ULL << i))) continue;

        kthread_t *target_k = p->kthread[vcpuid];
        if (!target_k || !target_k->ctrl.g) continue;
        if (!are_kthread_flags_set(target_k, KTHREAD_READY_F)) continue;

        {
            prtos_u8_t target_cpu = prtos_conf_vcpu_table[
                (part_id * prtos_conf_table.hpv.num_of_cpus) + vcpuid].cpu;
            if (target_cpu == GET_CPU_ID()) {
                /* Same pCPU: mark IPI pending */
                target_k->ctrl.g->karch.ipi_pending = 1;
            } else {
#ifdef CONFIG_SMP
                target_k->ctrl.g->karch.ipi_pending = 1;
                __asm__ __volatile__("dbar 0" ::: "memory");
                loongarch_send_ipi_to(target_cpu);
#endif
            }
        }
    }
    return RET_SUCCESS;
}

/*
 * prtos_iocsr_handle - Main dispatcher for hw-virt guest syscalls.
 *
 * Returns 1 if handled, 0 if not.
 */
int prtos_iocsr_handle(struct cpu_user_regs *regs) {
    prtos_u64_t ext_id = regs->a7;
    long error = RET_SUCCESS;
    long value = 0;

    switch (ext_id) {
    case PRTOS_EXT_CONSOLE:
        iocsr_console_putchar((int)regs->a0);
        regs->a0 = RET_SUCCESS;
        return 1;

    case PRTOS_EXT_TIMER: {
        /* Timer set: a0 = timer value in ticks */
        local_processor_t *info = GET_LOCAL_PROCESSOR();
        kthread_t *k = info->sched.current_kthread;
        if (k && k->ctrl.g) {
            k->ctrl.g->karch.guest_timer_active = 1;
            /* Program the hardware timer */
            prtos_u64_t tcfg = (1UL) | (regs->a0 << 2);  /* EN=1, PERIOD=0 */
            __asm__ __volatile__("csrwr %0, 0x41" : "+r"(tcfg));
        }
        regs->a0 = RET_SUCCESS;
        return 1;
    }

    case PRTOS_EXT_IPI:
        error = prtos_iocsr_send_ipi(regs->a0, regs->a1);
        break;

    case PRTOS_EXT_HSM:
        switch (regs->a6) {
        case HSM_VCPU_START:
            error = prtos_hsm_vcpu_start(regs->a0, regs->a1, regs->a2);
            break;
        case HSM_VCPU_STOP: {
            local_processor_t *info = GET_LOCAL_PROCESSOR();
            kthread_t *k = info->sched.current_kthread;
            set_kthread_flags(k, KTHREAD_HALTED_F);
            clear_kthread_flags(k, KTHREAD_READY_F);
            schedule();
            break;
        }
        case HSM_VCPU_GET_STATUS:
            error = prtos_hsm_vcpu_get_status(regs->a0);
            if (error >= 0) {
                value = error;
                error = RET_SUCCESS;
            }
            break;
        default:
            error = RET_ERR_NOT_SUPPORTED;
            break;
        }
        break;

    case PRTOS_EXT_BASE:
        /* Report implementation info */
        switch (regs->a6) {
        case 0: /* get spec version */
            value = (1UL << 24) | 0;
            break;
        case 3: /* probe extension */
            switch (regs->a0) {
            case PRTOS_EXT_CONSOLE:
            case PRTOS_EXT_TIMER:
            case PRTOS_EXT_IPI:
            case PRTOS_EXT_HSM:
            case PRTOS_EXT_BASE:
                value = 1;
                break;
            default:
                value = 0;
                break;
            }
            break;
        default:
            value = 0;
            break;
        }
        break;

    default:
        return 0;
    }

    regs->a0 = (prtos_u64_t)error;
    regs->a1 = (prtos_u64_t)value;
    return 1;
}
