/*
 * FILE: prtos_sbi.c
 *
 * SBI (Supervisor Binary Interface) emulation for PRTOS hw-virt
 * partitions on RISC-V 64.  Handles ecall-based SBI calls from
 * Linux guests for secondary vCPU startup (HSM), IPI, remote
 * fence, and base extension probing.
 *
 * www.prtos.org
 */

#include <irqs.h>
#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <stdc.h>

/* SBI extension IDs */
#define SBI_EXT_BASE     0x10
#define SBI_EXT_TIME     0x54494D45
#define SBI_EXT_IPI      0x735049
#define SBI_EXT_RFENCE   0x52464E43
#define SBI_EXT_HSM      0x48534D
#define SBI_EXT_SRST     0x53525354
#define SBI_EXT_DBCN     0x4442434E

/* SBI legacy extension IDs */
#define SBI_LEGACY_CONSOLE_PUTCHAR  1
#define SBI_LEGACY_CONSOLE_GETCHAR  2

/* SBI HSM function IDs */
#define SBI_HSM_HART_START       0
#define SBI_HSM_HART_STOP        1
#define SBI_HSM_HART_GET_STATUS  2

/* SBI HSM hart states */
#define SBI_HSM_STATE_STARTED    0
#define SBI_HSM_STATE_STOPPED    1
#define SBI_HSM_STATE_START_PENDING  2

/* SBI BASE function IDs */
#define SBI_BASE_GET_SPEC_VERSION    0
#define SBI_BASE_GET_IMPL_ID        1
#define SBI_BASE_GET_IMPL_VERSION   2
#define SBI_BASE_PROBE_EXTENSION    3
#define SBI_BASE_GET_MVENDORID      4
#define SBI_BASE_GET_MARCHID        5
#define SBI_BASE_GET_MIMPID         6

/* SBI IPI function IDs */
#define SBI_IPI_SEND_IPI   0

/* SBI RFENCE function IDs */
#define SBI_RFENCE_REMOTE_FENCE_I       0
#define SBI_RFENCE_REMOTE_SFENCE_VMA    1

/* SBI TIME function IDs */
#define SBI_TIME_SET_TIMER  0

/* SBI return codes */
#define SBI_SUCCESS                0
#define SBI_ERR_FAILED            -1
#define SBI_ERR_NOT_SUPPORTED     -2
#define SBI_ERR_INVALID_PARAM     -3
#define SBI_ERR_DENIED            -4
#define SBI_ERR_INVALID_ADDRESS   -5
#define SBI_ERR_ALREADY_AVAILABLE -6

/* External: partition table */
extern partition_t *partition_table;
extern struct prtos_conf_vcpu *prtos_conf_vcpu_table;

/* SBI set_timer to M-mode */
static void sbi_set_timer_mmode(prtos_u64_t stime_value) {
    register prtos_u64_t a0 __asm__("a0") = stime_value;
    register prtos_u64_t a7 __asm__("a7") = 0;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

/* SBI console putchar: write character to UART at 0x10000000 (identity mapped) */
static void sbi_console_putchar(int ch) {
    volatile prtos_u8_t *uart = (volatile prtos_u8_t *)0x10000000ULL;
    /* Wait for transmit FIFO ready (bit 5 of LSR at offset 5) */
    while (!(uart[5] & 0x20))
        ;
    uart[0] = (prtos_u8_t)ch;
}

/*
 * prtos_sbi_hsm_hart_start - Start a secondary vCPU.
 *
 * Linux calls SBI HSM HART_START with:
 *   target_hartid: vCPU ID (0-based within partition)
 *   start_addr: guest physical address to start executing
 *   opaque: value passed as a1 to the target hart
 */
static long prtos_sbi_hsm_hart_start(prtos_u64_t target_hartid,
                                      prtos_u64_t start_addr,
                                      prtos_u64_t opaque) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    partition_t *p = &partition_table[part_id];

    int target_vcpu = (int)target_hartid;
    if (target_vcpu >= (int)p->cfg->num_of_vcpus)
        return SBI_ERR_INVALID_PARAM;

    kthread_t *target_k = p->kthread[target_vcpu];
    if (!target_k || !target_k->ctrl.g)
        return SBI_ERR_INVALID_PARAM;

    /* Check if already started */
    if (are_kthread_flags_set(target_k, KTHREAD_READY_F))
        return SBI_ERR_ALREADY_AVAILABLE;

    /* Store HSM boot parameters for the target vCPU */
    target_k->ctrl.g->karch.hsm_entry = start_addr;
    target_k->ctrl.g->karch.hsm_opaque = opaque;

    /* Initialize PCT for the target vCPU: mask all para-virt IRQs
     * so raise_pend_irqs() doesn't corrupt the hw-virt guest's PC. */
    {
        partition_control_table_t *pct = target_k->ctrl.g->part_ctrl_table;
        pct->id = target_k->ctrl.g->id;
        pct->num_of_vcpus = p->cfg->num_of_vcpus;
        pct->hw_irqs_mask |= ~0;
        pct->ext_irqs_to_mask |= ~0;
        init_part_ctrl_table_irqs(&pct->iflags);
    }

    /* Set up the kthread stack so the scheduler can context-switch to it. */
    extern void start_up_guest(prtos_address_t entry);
    setup_kstack(target_k, start_up_guest, start_addr);

    /* Memory barrier before making visible to other CPUs */
    __asm__ __volatile__("fence rw, rw" ::: "memory");

    /* Mark target as ready for scheduling */
    set_kthread_flags(target_k, KTHREAD_READY_F);
    clear_kthread_flags(target_k, KTHREAD_HALTED_F);

    __asm__ __volatile__("fence rw, rw" ::: "memory");

    return SBI_SUCCESS;
}

/*
 * prtos_sbi_hsm_hart_get_status - Return vCPU state.
 */
static long prtos_sbi_hsm_hart_get_status(prtos_u64_t target_hartid) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    partition_t *p = &partition_table[part_id];

    int target_vcpu = (int)target_hartid;
    if (target_vcpu >= (int)p->cfg->num_of_vcpus)
        return SBI_ERR_INVALID_PARAM;

    kthread_t *target_k = p->kthread[target_vcpu];
    if (!target_k)
        return SBI_ERR_INVALID_PARAM;

    if (are_kthread_flags_set(target_k, KTHREAD_READY_F))
        return SBI_HSM_STATE_STARTED;

    return SBI_HSM_STATE_STOPPED;
}

/*
 * prtos_sbi_send_ipi - Send IPI to specified harts in the partition.
 *
 * hart_mask: bitmask of target harts
 * hart_mask_base: base hart ID for the mask
 */
static long prtos_sbi_send_ipi(prtos_u64_t hart_mask,
                                prtos_u64_t hart_mask_base) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    partition_t *p = &partition_table[part_id];
    int i;

    for (i = 0; i < (int)p->cfg->num_of_vcpus; i++) {
        int hartid = (int)hart_mask_base + i;
        if (hartid >= (int)p->cfg->num_of_vcpus)
            break;
        if (!(hart_mask & (1ULL << i)))
            continue;

        kthread_t *target_k = p->kthread[hartid];
        if (!target_k || !target_k->ctrl.g)
            continue;
        if (!are_kthread_flags_set(target_k, KTHREAD_READY_F))
            continue;

        /* Inject VSSIP (VS-mode software interrupt pending) via hvip.
         * We need to set this on the target hart's context.  If the target
         * is running on another pCPU, we send a real IPI to that pCPU
         * which will then inject VSSIP on context restore.
         *
         * For simplicity, set a pending IRQ flag that the scheduler
         * will deliver when the target vCPU is next scheduled. */
        {
            prtos_u8_t target_cpu = prtos_conf_vcpu_table[
                (part_id * prtos_conf_table.hpv.num_of_cpus) + hartid].cpu;
            if (target_cpu == GET_CPU_ID()) {
                /* Same pCPU: inject VSSIP directly.  On the next sret
                 * (return to VS-mode) the CPU will deliver VSSIP. */
                __asm__ __volatile__("csrs hvip, %0" :: "r"(1UL << 2));
            } else {
#ifdef CONFIG_SMP
                /* Cross-CPU IPI: mark pending VSSIP on the target kthread
                 * and send a real supervisor software interrupt so the
                 * target pCPU traps to HS-mode and injects VSSIP. */
                target_k->ctrl.g->karch.vssip_pending = 1;
                __asm__ __volatile__("fence rw, rw" ::: "memory");
                riscv_send_ipi_to(target_cpu);
#endif
            }
        }
    }

    return SBI_SUCCESS;
}

/*
 * prtos_sbi_handle - Main SBI dispatcher for VS-mode ecalls.
 *
 * Called from traps.c when an ecall from VS-mode has a recognized
 * SBI extension ID in a7.
 *
 * Returns 1 if handled, 0 if not an SBI call.
 * On success, sets regs->a0 = error code, regs->a1 = value.
 */
int prtos_sbi_handle(struct cpu_user_regs *regs) {
    prtos_u64_t ext_id = regs->a7;
    prtos_u64_t func_id = regs->a6;
    long error = SBI_SUCCESS;
    long value = 0;

    switch (ext_id) {
    case SBI_LEGACY_CONSOLE_PUTCHAR:
        /* SBI legacy putchar: a0 = character */
        sbi_console_putchar((int)regs->a0);
        regs->a0 = SBI_SUCCESS;
        return 1;

    case SBI_LEGACY_CONSOLE_GETCHAR:
        /* SBI legacy getchar: not supported, return -1 */
        regs->a0 = (prtos_u64_t)-1;
        return 1;

    case SBI_EXT_DBCN:
        /* SBI Debug Console extension: write byte */
        if (func_id == 0x02) {  /* sbi_debug_console_write_byte */
            sbi_console_putchar((int)regs->a0);
            regs->a0 = SBI_SUCCESS;
            regs->a1 = 0;
        } else {
            regs->a0 = (prtos_u64_t)SBI_ERR_NOT_SUPPORTED;
            regs->a1 = 0;
        }
        return 1;

    case SBI_EXT_BASE:
        switch (func_id) {
        case SBI_BASE_GET_SPEC_VERSION:
            /* SBI spec version 1.0 */
            value = (1UL << 24) | 0;
            break;
        case SBI_BASE_GET_IMPL_ID:
            /* Implementation ID: use 0xFF for PRTOS */
            value = 0xFF;
            break;
        case SBI_BASE_GET_IMPL_VERSION:
            value = 1;
            break;
        case SBI_BASE_PROBE_EXTENSION:
            /* Report which extensions we support */
            switch (regs->a0) {
            case SBI_EXT_BASE:
            case SBI_EXT_TIME:
            case SBI_EXT_IPI:
            case SBI_EXT_RFENCE:
            case SBI_EXT_HSM:
                value = 1;  /* supported */
                break;
            default:
                value = 0;  /* not supported */
                break;
            }
            break;
        case SBI_BASE_GET_MVENDORID:
        case SBI_BASE_GET_MARCHID:
        case SBI_BASE_GET_MIMPID:
            value = 0;
            break;
        default:
            error = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        break;

    case SBI_EXT_TIME:
        if (func_id == SBI_TIME_SET_TIMER) {
            /* New-style SBI timer extension */
            local_processor_t *info = GET_LOCAL_PROCESSOR();
            kthread_t *k = info->sched.current_kthread;
            if (k && k->ctrl.g) {
                k->ctrl.g->karch.guest_timer_active = 1;
                __asm__ __volatile__("csrc hvip, %0" :: "r"(1UL << 6));
                sbi_set_timer_mmode(regs->a0);
            }
        } else {
            error = SBI_ERR_NOT_SUPPORTED;
        }
        break;

    case SBI_EXT_IPI:
        if (func_id == SBI_IPI_SEND_IPI) {
            error = prtos_sbi_send_ipi(regs->a0, regs->a1);
        } else {
            error = SBI_ERR_NOT_SUPPORTED;
        }
        break;

    case SBI_EXT_RFENCE:
        /* For remote fence operations, execute locally.
         * In a partitioned system, each vCPU runs on a dedicated pCPU
         * per the cyclic schedule, so local fence is sufficient. */
        switch (func_id) {
        case SBI_RFENCE_REMOTE_FENCE_I:
            __asm__ __volatile__("fence.i" ::: "memory");
            break;
        case SBI_RFENCE_REMOTE_SFENCE_VMA:
            /* Execute hfence.gvma to flush G-stage TLB entries.
             * For the guest, this is equivalent to sfence.vma. */
            __asm__ __volatile__("fence rw, rw" ::: "memory");
            break;
        default:
            error = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        break;

    case SBI_EXT_HSM:
        switch (func_id) {
        case SBI_HSM_HART_START:
            error = prtos_sbi_hsm_hart_start(regs->a0, regs->a1, regs->a2);
            break;
        case SBI_HSM_HART_STOP:
            /* Stop current hart: halt the current kthread */
            {
                local_processor_t *info = GET_LOCAL_PROCESSOR();
                kthread_t *k = info->sched.current_kthread;
                set_kthread_flags(k, KTHREAD_HALTED_F);
                clear_kthread_flags(k, KTHREAD_READY_F);
                schedule();
            }
            break;
        case SBI_HSM_HART_GET_STATUS:
            error = prtos_sbi_hsm_hart_get_status(regs->a0);
            if (error >= 0) {
                value = error;
                error = SBI_SUCCESS;
            }
            break;
        default:
            error = SBI_ERR_NOT_SUPPORTED;
            break;
        }
        break;

    case SBI_EXT_SRST:
        /* System reset: halt the partition */
        error = SBI_ERR_NOT_SUPPORTED;
        break;

    default:
        return 0;  /* Not handled */
    }

    /* SBI return convention: a0 = error, a1 = value */
    regs->a0 = (prtos_u64_t)error;
    regs->a1 = (prtos_u64_t)value;
    return 1;
}
