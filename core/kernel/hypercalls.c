/*
 * FILE: hypercalls.c
 *
 * prtos's hypercalls
 *
 * www.prtos.org
 */

#include <assert.h>
#include <kthread.h>
#include <gaccess.h>
#include <physmm.h>
#include <processor.h>
#include <spinlock.h>
#include <sched.h>
#include <smp.h>
#include <stdc.h>
#include <hypercalls.h>
#include <virtmm.h>
#include <vmmap.h>

#include <objects/trace.h>

extern prtos_u32_t reset_status_init[];

extern struct {
    prtos_u32_t num_of_args;
#define HYP_NO_ARGS(args) ((args) & ~0x80000000)
} hypercall_flags_table[NR_HYPERCALLS];

__hypercall prtos_s32_t multi_call_sys(void *__g_param start_addr, void *__g_param end_addr) {
#define BATCH_GET_PARAM(_addr, _arg) *(prtos_u32_t *)((_addr) + sizeof(prtos_u32_t) * (2 + (_arg)))
    extern prtos_s32_t (*hypercallsTab[NR_HYPERCALLS])(prtos_word_t, ...);
    prtos_address_t addr;
    prtos_u32_t hypercall_nr;

    ASSERT(!hw_is_sti());
    if (end_addr < start_addr) return PRTOS_INVALID_PARAM;

    if (check_gp_aram(start_addr, (prtos_address_t)end_addr - (prtos_address_t)start_addr, 4, PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;

    for (addr = (prtos_address_t)start_addr; addr < (prtos_address_t)end_addr;) {
        hypercall_nr = *(prtos_u32_t *)addr;
        *(prtos_u32_t *)(addr + sizeof(prtos_u32_t)) &= ~(0xffff << 16);
        if ((hypercall_nr >= NR_HYPERCALLS) || (*(prtos_u32_t *)(addr + sizeof(prtos_u32_t)) != HYP_NO_ARGS(hypercall_flags_table[hypercall_nr].num_of_args))) {
            *(prtos_u32_t *)(addr + sizeof(prtos_u32_t)) |= (PRTOS_INVALID_PARAM << 16);
            PWARN("[MULTICALL] hyp %d no. params mismatches\n", hypercall_nr);
            return PRTOS_MULTICALL_ERROR;
        }
        *(prtos_u32_t *)(addr + sizeof(prtos_u32_t)) |=
            hypercallsTab[hypercall_nr](BATCH_GET_PARAM(addr, 0), BATCH_GET_PARAM(addr, 1), BATCH_GET_PARAM(addr, 2), BATCH_GET_PARAM(addr, 3),
                                        BATCH_GET_PARAM(addr, 4))
            << 16;
        addr += (HYP_NO_ARGS(hypercall_flags_table[hypercall_nr].num_of_args) + 2) * sizeof(prtos_u32_t);
    }

#undef BATCH_GET_PARAM
    return PRTOS_OK;
}

__hypercall prtos_s32_t halt_part_sys(prtos_id_t partition_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    cpu_ctxt_t ctxt;
    prtos_s32_t e;

    ASSERT(!hw_is_sti());
    if (partition_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id)) {
        if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

        if (partition_id >= prtos_conf_table.num_of_partitions) return PRTOS_INVALID_PARAM;

        for (e = 0; e < part_table[partition_id].cfg->num_of_vcpus; e++)
            if (!are_kthread_flags_set(part_table[partition_id].kthread[e], KTHREAD_HALTED_F)) break;

        if (e >= part_table[partition_id].cfg->num_of_vcpus) return PRTOS_NO_ACTION;

        HALT_PARTITION(partition_id);
#ifdef CONFIG_DEBUG
        kprintf("[HYPERCALL] (0x%x) Halted\n", partition_id);
#endif
        return PRTOS_OK;
    }

    HALT_PARTITION(partition_id);
#ifdef CONFIG_DEBUG
    kprintf("[HYPERCALL] (0x%x) Halted\n", partition_id);
#endif
    schedule();
    get_cpu_ctxt(&ctxt);
    system_panic(&ctxt, "[HYPERCALL] A halted partition is being executed");

    return PRTOS_OK;
}

__hypercall prtos_s32_t suspend_vcpu_sys(prtos_id_t vcpu_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k;

    ASSERT(!hw_is_sti());

    if (vcpu_id >= get_partition(info->sched.current_kthread)->cfg->num_of_vcpus) return PRTOS_INVALID_PARAM;

    k = get_partition(info->sched.current_kthread)->kthread[vcpu_id];

    if (!are_kthread_flags_set(k, KTHREAD_SUSPENDED_F | KTHREAD_HALTED_F)) return PRTOS_NO_ACTION;

    SUSPEND_VCPU(KID2PARTID(k->ctrl.g->id), KID2VCPUID(k->ctrl.g->id));

    if (k == info->sched.current_kthread) schedule();

    return PRTOS_OK;
}

__hypercall prtos_s32_t resume_vcpu_sys(prtos_id_t vcpu_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k;

    ASSERT(!hw_is_sti());

    if (vcpu_id >= get_partition(info->sched.current_kthread)->cfg->num_of_vcpus) return PRTOS_INVALID_PARAM;

    k = get_partition(info->sched.current_kthread)->kthread[vcpu_id];
    if (are_kthread_flags_set(k, KTHREAD_SUSPENDED_F) && !are_kthread_flags_set(k, KTHREAD_HALTED_F)) return PRTOS_NO_ACTION;

    RESUME_VCPU(KID2PARTID(k->ctrl.g->id), KID2VCPUID(k->ctrl.g->id));

    if (k == info->sched.current_kthread) schedule();
#ifdef CONFIG_SMP
    else {
        prtos_u8_t cpu = prtos_conf_vcpu_table[(KID2PARTID(info->sched.current_kthread->ctrl.g->id) * prtos_conf_table.hpv.num_of_cpus) + vcpu_id].cpu;
        if (cpu != GET_CPU_ID()) send_ipi(cpu, NO_SHORTHAND_IPI, SCHED_PENDING_IPI_VECTOR);
    }
#endif

    return PRTOS_OK;
}

__hypercall prtos_s32_t reset_vcpu_sys(prtos_id_t vcpu_id, prtos_address_t ptd_level_1_table, prtos_address_t entry_point, prtos_u32_t status) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    partition_t *partition = get_partition(info->sched.current_kthread);
    struct phys_page *ptd_level_1_page;

    if (vcpu_id >= partition->cfg->num_of_vcpus) return PRTOS_INVALID_PARAM;

    if (!(ptd_level_1_page = pmm_find_page(ptd_level_1_table, partition, 0))) return PRTOS_INVALID_PARAM;

    if (ptd_level_1_page->type != PPAG_PTDL1) return PRTOS_INVALID_PARAM;

    HALT_VCPU(partition->cfg->id, vcpu_id);

    reset_kthread(partition->kthread[vcpu_id], ptd_level_1_table, entry_point, status);

    return PRTOS_OK;
}

__hypercall prtos_id_t get_vcpuid_sys(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    return KID2VCPUID(info->sched.current_kthread->ctrl.g->id);
}

__hypercall prtos_s32_t halt_vcpu_sys(prtos_id_t vcpu_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k;

    ASSERT(!hw_is_sti());

    if (vcpu_id >= get_partition(info->sched.current_kthread)->cfg->num_of_vcpus) return PRTOS_INVALID_PARAM;

    k = get_partition(info->sched.current_kthread)->kthread[vcpu_id];
#ifdef PRTOS_VERBOSE
    kprintf("Partition[%d]:vCPU[%d] halted\n", get_partition(info->sched.current_kthread)->cfg->id, vcpu_id);
#endif
    if (are_kthread_flags_set(k, KTHREAD_HALTED_F)) return PRTOS_NO_ACTION;

    HALT_VCPU(KID2PARTID(k->ctrl.g->id), KID2VCPUID(k->ctrl.g->id));

    if (k == info->sched.current_kthread) schedule();

    return PRTOS_OK;
}

__hypercall prtos_s32_t halt_system_sys(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    extern void halt_system(void);

    ASSERT(!hw_is_sti());

    if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    hw_cli();
    halt_system();
    return PRTOS_OK;
}

// the ".data" section is restored during the initialisation
__hypercall prtos_s32_t reset_system_sys(prtos_u32_t reset_mode) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    ASSERT(!hw_is_sti());
    if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if ((reset_mode != PRTOS_COLD_RESET) && (reset_mode != PRTOS_WARM_RESET)) return PRTOS_INVALID_PARAM;
    reset_status_init[0] = (PRTOS_RESET_STATUS_PARTITION_NORMAL_START << PRTOS_HM_RESET_STATUS_USER_CODE_BIT);
    reset_system(reset_mode);

    return PRTOS_OK;
}

__hypercall prtos_s32_t flush_cache_sys(prtos_u32_t cache) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    ASSERT(!hw_is_sti());
    if (cache & ~(PRTOS_DCACHE | PRTOS_ICACHE)) return PRTOS_INVALID_PARAM;
    set_kthread_flags(info->sched.current_kthread, (cache & KTHREAD_FLUSH_CACHE_W) << KTHREAD_FLUSH_CACHE_B);
    return PRTOS_OK;
}

__hypercall prtos_s32_t set_cache_state_sys(prtos_u32_t cache) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    ASSERT(!hw_is_sti());
    if (cache & ~(PRTOS_DCACHE | PRTOS_ICACHE)) return PRTOS_INVALID_PARAM;

    clear_kthread_flags(info->sched.current_kthread, KTHREAD_CACHE_ENABLED_W);
    set_kthread_flags(info->sched.current_kthread, (cache & KTHREAD_CACHE_ENABLED_W) << KTHREAD_CACHE_ENABLED_B);

    return PRTOS_OK;
}

__hypercall prtos_s32_t switch_sched_plan_sys(prtos_u32_t new_plan_id, prtos_u32_t *__g_param current_plan_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    ASSERT(!hw_is_sti());

    if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if (check_gp_aram(current_plan_id, sizeof(prtos_u32_t), 4, PFLAG_RW | PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    if (info->sched.data->plan.current->id == new_plan_id) return PRTOS_NO_ACTION;

    if ((new_plan_id < 0) || (new_plan_id >= prtos_conf_table.hpv.cpu_table[GET_CPU_ID()].num_of_sched_cyclic_plans)) return PRTOS_INVALID_PARAM;

    if (switch_sched_plan(new_plan_id, current_plan_id)) return PRTOS_INVALID_CONFIG;

    return PRTOS_OK;
}

__hypercall prtos_s32_t suspend_part_sys(prtos_id_t partition_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_s32_t e;
    ASSERT(!hw_is_sti());
    if (partition_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id)) {
        if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

        if (partition_id >= prtos_conf_table.num_of_partitions) return PRTOS_INVALID_PARAM;

        for (e = 0; e < part_table[partition_id].cfg->num_of_vcpus; e++)
            if (!are_kthread_flags_set(part_table[partition_id].kthread[e], KTHREAD_SUSPENDED_F | KTHREAD_HALTED_F)) break;

        if (e >= part_table[partition_id].cfg->num_of_vcpus) return PRTOS_NO_ACTION;

        SUSPEND_PARTITION(partition_id);
        return PRTOS_OK;
    }

    SUSPEND_PARTITION(partition_id);
    schedule();
    return PRTOS_OK;
}

__hypercall prtos_s32_t resume_part_sys(prtos_id_t partition_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_s32_t e;
    ASSERT(!hw_is_sti());

    if (partition_id == KID2PARTID(info->sched.current_kthread->ctrl.g->id)) return PRTOS_NO_ACTION;

    if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if (partition_id >= prtos_conf_table.num_of_partitions) return PRTOS_INVALID_PARAM;

    for (e = 0; e < part_table[partition_id].cfg->num_of_vcpus; e++)
        if (are_kthread_flags_set(part_table[partition_id].kthread[e], KTHREAD_SUSPENDED_F) &&
            !are_kthread_flags_set(part_table[partition_id].kthread[e], KTHREAD_HALTED_F))
            break;

    if (e >= part_table[partition_id].cfg->num_of_vcpus) return PRTOS_NO_ACTION;

    RESUME_PARTITION(partition_id);
    return PRTOS_OK;
}

__hypercall prtos_s32_t reset_partition_sys(prtos_id_t partition_id, prtos_u32_t reset_mode, prtos_u32_t status) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    ASSERT(!hw_is_sti());
    if (partition_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id)) {
        if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;
        if (partition_id >= prtos_conf_table.num_of_partitions) return PRTOS_INVALID_PARAM;
    }
    if ((reset_mode == PRTOS_WARM_RESET) || (reset_mode == PRTOS_COLD_RESET)) {
        if (!reset_partition(&part_table[partition_id], reset_mode & PRTOS_RESET_MODE, status))
            return PRTOS_OK;
        else
            return PRTOS_INVALID_CONFIG;
    }

    return PRTOS_INVALID_PARAM;
}

__hypercall prtos_s32_t shutdown_partition_sys(prtos_id_t partition_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    ASSERT(!hw_is_sti());
    if (partition_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id)) {
        if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;
        if (partition_id >= prtos_conf_table.num_of_partitions) return PRTOS_INVALID_PARAM;
    }

    SHUTDOWN_PARTITION(partition_id);
    return PRTOS_OK;
}

__hypercall prtos_s32_t idle_self_sys(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
#ifdef CONFIG_AUDIT_EVENTS
    prtos_word_t arg = KID2PARTID(info->sched.current_kthread->ctrl.g->id);
#endif
    ASSERT(!hw_is_sti());

#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_PART_IDLE, 1, &arg);
#endif
    sched_yield(info, info->sched.idle_kthread);
    return PRTOS_OK;
}

__hypercall prtos_s32_t set_timer_sys(prtos_u32_t clock_id, prtos_time_t abstime, prtos_time_t interval) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_s32_t ret = PRTOS_OK;

    ASSERT(!hw_is_sti());

    if ((abstime < 0) || (interval < 0)) return PRTOS_INVALID_PARAM;

    // Disarming a timer
    if (!abstime) {
        switch (clock_id) {
            case PRTOS_HW_CLOCK:
                disarm_ktimer(&info->sched.current_kthread->ctrl.g->ktimer);
                return PRTOS_OK;
            case PRTOS_EXEC_CLOCK:
                disarm_vtimer(&info->sched.current_kthread->ctrl.g->vtimer, &info->sched.current_kthread->ctrl.g->vclock);
                return PRTOS_OK;
            case PRTOS_WATCHDOG_TIMER:
                disarm_ktimer(&info->sched.current_kthread->ctrl.g->watchdogTimer);
                return PRTOS_OK;
            default:
                return PRTOS_INVALID_PARAM;
        }
    }
    // Arming a timer
    switch (clock_id) {
        case PRTOS_HW_CLOCK:
            ret = arm_ktimer(&info->sched.current_kthread->ctrl.g->ktimer, abstime, interval);
            break;
        case PRTOS_EXEC_CLOCK:
            ret = arm_vtimer(&info->sched.current_kthread->ctrl.g->vtimer, &info->sched.current_kthread->ctrl.g->vclock, abstime, interval);
            break;
        case PRTOS_WATCHDOG_TIMER:
            ret = arm_ktimer(&info->sched.current_kthread->ctrl.g->watchdogTimer, abstime, interval);
            break;
        default:
            return PRTOS_INVALID_PARAM;
    }

    return ret;
}

__hypercall prtos_s32_t get_time_sys(prtos_u32_t clock_id, prtos_time_t *__g_param time) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    if (check_gp_aram(time, sizeof(prtos_s64_t), 8, PFLAG_RW | PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    switch (clock_id) {
        case PRTOS_HW_CLOCK:
            *time = get_sys_clock_usec();
            break;
        case PRTOS_EXEC_CLOCK:
            *time = get_time_usec_vclock(&info->sched.current_kthread->ctrl.g->vclock);
            break;
        default:
            return PRTOS_INVALID_PARAM;
    }

    return PRTOS_OK;
}

__hypercall prtos_s32_t clear_irq_mask_sys(prtos_u32_t hw_irqs_mask, prtos_u32_t ext_irqs_pend) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_u32_t unmasked;
    prtos_s32_t e;

    ASSERT(!hw_is_sti());
    info->sched.current_kthread->ctrl.g->part_ctrl_table->hw_irqs_mask &= ~hw_irqs_mask;
    info->sched.current_kthread->ctrl.g->part_ctrl_table->ext_irqs_to_mask &= ~ext_irqs_pend;
    unmasked = hw_irqs_mask & get_partition(info->sched.current_kthread)->cfg->hw_irqs;
    for (e = 0; unmasked; e++)
        if (unmasked & (1 << e)) {
            hw_enable_irq(e);
            unmasked &= ~(1 << e);
        }

    return PRTOS_OK;
}

__hypercall prtos_s32_t set_irq_mask_sys(prtos_u32_t hw_irqs_mask, prtos_u32_t ext_irqs_pend) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_u32_t masked;
    prtos_s32_t e;

    ASSERT(!hw_is_sti());
    info->sched.current_kthread->ctrl.g->part_ctrl_table->hw_irqs_mask |= hw_irqs_mask;
    info->sched.current_kthread->ctrl.g->part_ctrl_table->ext_irqs_to_mask |= ext_irqs_pend;
    masked = hw_irqs_mask & get_partition(info->sched.current_kthread)->cfg->hw_irqs;
    for (e = 0; masked; e++)
        if (masked & (1 << e)) {
            hw_disable_irq(e);
            masked &= ~(1 << e);
        }
    return PRTOS_OK;
}

__hypercall prtos_s32_t force_irqs_sys(prtos_u32_t hw_irq_mask, prtos_u32_t ext_irq_mask) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_u32_t forced;
    prtos_s32_t e;

    ASSERT(!hw_is_sti());

    //    info->sched.current_kthread->ctrl.g->part_ctrl_table->hw_irqs_pend|=hw_irq_mask;
    info->sched.current_kthread->ctrl.g->part_ctrl_table->ext_irqs_pend |= ext_irq_mask;
    forced = hw_irq_mask & get_partition(info->sched.current_kthread)->cfg->hw_irqs;
    hw_irq_mask &= ~forced;
    for (e = 0; forced; e++)
        if (forced & (1 << e)) {
            hw_force_irq(e);
            forced &= ~(1 << e);
        }
    for (e = 0; hw_irq_mask; e++)
        if (hw_irq_mask & (1 << e)) {
            info->sched.current_kthread->ctrl.g->part_ctrl_table->hw_irqs_pend |= (1 << e);
            hw_irq_mask &= ~(1 << e);
        }
    return PRTOS_OK;
}

__hypercall prtos_s32_t clear_irqs_sys(prtos_u32_t hw_irq_mask, prtos_u32_t ext_irq_mask) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_u32_t pending;
    prtos_s32_t e;

    ASSERT(!hw_is_sti());

    info->sched.current_kthread->ctrl.g->part_ctrl_table->hw_irqs_pend &= ~hw_irq_mask;
    info->sched.current_kthread->ctrl.g->part_ctrl_table->ext_irqs_pend &= ~ext_irq_mask;
    pending = hw_irq_mask & get_partition(info->sched.current_kthread)->cfg->hw_irqs;
    for (e = 0; pending; e++)
        if (pending & (1 << e)) {
            hw_clear_irq(e);
            pending &= ~(1 << e);
        }
    return PRTOS_OK;
}

__hypercall prtos_s32_t route_irq_sys(prtos_u32_t type, prtos_u32_t irq, prtos_u16_t vector) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    ASSERT(!hw_is_sti());

    if (irq >= 32) return PRTOS_INVALID_PARAM;
    switch (type) {
        case PRTOS_TRAP_TYPE:
            info->sched.current_kthread->ctrl.g->part_ctrl_table->trap_to_vector[irq] = vector;
            break;
        case PRTOS_HWIRQ_TYPE:
            info->sched.current_kthread->ctrl.g->part_ctrl_table->hw_irq_to_vector[irq] = vector;
            break;
        case PRTOS_EXTIRQ_TYPE:
            info->sched.current_kthread->ctrl.g->part_ctrl_table->ext_irq_to_vector[irq] = vector;
            break;
        default:
            return PRTOS_INVALID_PARAM;
    }

    return PRTOS_OK;
}

__hypercall prtos_s32_t read_object_sys(prtos_obj_desc_t obj_desc, void *__g_param buffer, prtos_u_size_t size, prtos_u32_t *__g_param flags) {
    prtos_u32_t class;

    ASSERT(!hw_is_sti());

    class = OBJDESC_GET_CLASS(obj_desc);
    if (class < OBJ_NO_CLASSES) {
        if (object_table[class] && object_table[class]->read) {
            return object_table[class]->read(obj_desc, buffer, size, flags);
        } else {
            return PRTOS_OP_NOT_ALLOWED;
        }
    }

    return PRTOS_INVALID_PARAM;
}

__hypercall prtos_s32_t write_object_sys(prtos_obj_desc_t obj_desc, void *__g_param buffer, prtos_u_size_t size, prtos_u32_t *__g_param flags) {
    prtos_u32_t class;

    ASSERT(!hw_is_sti());
    class = OBJDESC_GET_CLASS(obj_desc);
    if (class < OBJ_NO_CLASSES) {
        if (object_table[class] && object_table[class]->write) {
            return object_table[class]->write(obj_desc, buffer, size, flags);
        } else {
            return PRTOS_OP_NOT_ALLOWED;
        }
    }

    return PRTOS_INVALID_PARAM;
}

__hypercall prtos_s32_t seek_object_sys(prtos_obj_desc_t obj_desc, prtos_address_t offset, prtos_u32_t whence) {
    prtos_u32_t class;

    ASSERT(!hw_is_sti());

    class = OBJDESC_GET_CLASS(obj_desc);
    if (class < OBJ_NO_CLASSES) {
        if (object_table[class] && object_table[class]->seek) {
            return object_table[class]->seek(obj_desc, offset, whence);
        } else {
            return PRTOS_OP_NOT_ALLOWED;
        }
    }
    return PRTOS_INVALID_PARAM;
}

__hypercall prtos_s32_t ctrl_object_sys(prtos_obj_desc_t obj_desc, prtos_u32_t cmd, void *__g_param arg) {
    prtos_u32_t class;

    ASSERT(!hw_is_sti());

    class = OBJDESC_GET_CLASS(obj_desc);
    if (class < OBJ_NO_CLASSES) {
        if (object_table[class] && object_table[class]->ctrl) {
            return object_table[class]->ctrl(obj_desc, cmd, arg);
        } else {
            return PRTOS_OP_NOT_ALLOWED;
        }
    }

    return PRTOS_INVALID_PARAM;
}

__hypercall prtos_s32_t raise_part_ipvi_sys(prtos_id_t partition_id, prtos_u8_t ipvi_number) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct prtos_conf_part_ipvi *ipvi;
    kthread_t *k;
    prtos_s32_t e, vcpu;
    ASSERT(!hw_is_sti());

    if ((partition_id < 0) && (partition_id >= prtos_conf_table.num_of_partitions)) return PRTOS_INVALID_PARAM;

    if ((ipvi_number < PRTOS_VT_EXT_IPVI0) || (ipvi_number >= PRTOS_VT_EXT_IPVI0 + CONFIG_PRTOS_MAX_IPVI)) return PRTOS_INVALID_PARAM;

    ipvi = &get_partition(info->sched.current_kthread)->cfg->ipvi_table[ipvi_number - PRTOS_VT_EXT_IPVI0];
    if (ipvi->num_of_dsts <= 0) return PRTOS_INVALID_CONFIG;

    for (e = 0; e < ipvi->num_of_dsts; e++) {
        if (partition_id == prtos_conf_dst_ipvi[ipvi->dst_offset + e]) {
            partition_t *p = &part_table[prtos_conf_dst_ipvi[ipvi->dst_offset + e]];
            if (are_part_ext_irq_pending_set(p, ipvi_number)) return PRTOS_NO_ACTION;

            //           set_part_ext_irq_pending(p, ipvi_number);
            for (vcpu = 0; vcpu < p->cfg->num_of_vcpus; vcpu++) {
                k = p->kthread[vcpu];

                if (are_kthread_flags_set(k, KTHREAD_HALTED_F)) continue;
                if (are_ext_irq_pending_set(k, ipvi_number)) continue;
                set_ext_irq_pending(k, ipvi_number);
#ifdef CONFIG_SMP
                prtos_u8_t cpu = prtos_conf_vcpu_table[(KID2PARTID(k->ctrl.g->id) * prtos_conf_table.hpv.num_of_cpus) + KID2VCPUID(k->ctrl.g->id)].cpu;
                if (cpu != GET_CPU_ID()) send_ipi(cpu, NO_SHORTHAND_IPI, SCHED_PENDING_IPI_VECTOR);
#endif
            }
            return PRTOS_OK;
        }
    }

    return PRTOS_INVALID_CONFIG;
}

__hypercall prtos_s32_t raise_ipvi_sys(prtos_u8_t ipvi_number) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct prtos_conf_part_ipvi *ipvi;
    kthread_t *k;
    prtos_s32_t e, vcpu;
    ASSERT(!hw_is_sti());

    if ((ipvi_number < PRTOS_VT_EXT_IPVI0) || (ipvi_number >= PRTOS_VT_EXT_IPVI0 + CONFIG_PRTOS_MAX_IPVI)) return PRTOS_INVALID_PARAM;
    ipvi = &get_partition(info->sched.current_kthread)->cfg->ipvi_table[ipvi_number - PRTOS_VT_EXT_IPVI0];
    if (ipvi->num_of_dsts <= 0) return PRTOS_NO_ACTION;

    for (e = 0; e < ipvi->num_of_dsts; e++) {
        partition_t *p = &part_table[prtos_conf_dst_ipvi[ipvi->dst_offset + e]];

        //        set_part_ext_irq_pending(p, ipvi_number);
        for (vcpu = 0; vcpu < p->cfg->num_of_vcpus; vcpu++) {
            k = p->kthread[vcpu];

            if (are_kthread_flags_set(k, KTHREAD_HALTED_F)) continue;
            if (are_ext_irq_pending_set(k, ipvi_number)) continue;
            set_ext_irq_pending(k, ipvi_number);
#ifdef CONFIG_SMP
            prtos_u8_t cpu = prtos_conf_vcpu_table[(KID2PARTID(k->ctrl.g->id) * prtos_conf_table.hpv.num_of_cpus) + KID2VCPUID(k->ctrl.g->id)].cpu;
            if (cpu != GET_CPU_ID()) send_ipi(cpu, NO_SHORTHAND_IPI, SCHED_PENDING_IPI_VECTOR);
#endif
        }
    }

    return PRTOS_OK;
}

__hypercall prtos_s32_t get_gid_by_name_sys(prtos_u8_t *__g_param name, prtos_u32_t entity) {
    prtos_s32_t e, id = PRTOS_INVALID_CONFIG;

    if (check_gp_aram(name, CONFIG_ID_STRING_LENGTH, 1, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    switch (entity) {
        case PRTOS_PARTITION_NAME:
            for (e = 0; e < prtos_conf_table.num_of_partitions; e++)
                if (!strncmp(&prtos_conf_string_tab[prtos_conf_part_table[e].name_offset], name, CONFIG_ID_STRING_LENGTH)) {
                    id = prtos_conf_part_table[e].id;
                    break;
                }
            break;
        case PRTOS_PLAN_NAME:
            for (e = 0; e < prtos_conf_table.num_of_sched_cyclic_plans; e++)
                if (!strncmp(&prtos_conf_string_tab[prtos_conf_sched_cyclic_plan_table[e].name_offset], name, CONFIG_ID_STRING_LENGTH)) {
                    id = prtos_conf_sched_cyclic_plan_table[e].id;
                    break;
                }
            break;
        default:
            return PRTOS_INVALID_PARAM;
    }

    return id;
}

#ifdef CONFIG_AUDIT_EVENTS
void audit_hcall(prtos_u32_t hyp_number, ...) {
    va_list arg_ptr;
    prtos_u32_t e, num_of_args;
    prtos_word_t argList[6];

    if (is_audit_event_masked(TRACE_HCALLS_MODULE)) {
        if (hyp_number < NR_HYPERCALLS)
            num_of_args = HYP_NO_ARGS(hypercall_flags_table[hyp_number].num_of_args);
        else
            num_of_args = 0;
        ASSERT(num_of_args <= 5);
        argList[0] = (hyp_number << 16) | num_of_args;
        va_start(arg_ptr, hyp_number);
        for (e = 0; e < num_of_args; e++) argList[e + 1] = va_arg(arg_ptr, prtos_u32_t);
        va_end(arg_ptr);
        if (num_of_args < PRTOSTRACE_PAYLOAD_LENGTH)
            raise_audit_event(TRACE_HCALLS_MODULE, AUDIT_HCALL_BEGIN, num_of_args + 1, argList);
        else {
            raise_audit_event(TRACE_HCALLS_MODULE, AUDIT_HCALL_BEGIN, PRTOSTRACE_PAYLOAD_LENGTH, argList);
            raise_audit_event(TRACE_HCALLS_MODULE, AUDIT_HCALL_BEGIN2, num_of_args - (PRTOSTRACE_PAYLOAD_LENGTH - 1), &argList[3]);
        }
    }
}

void audit_hypercall_ret(prtos_word_t ret_val) {
    raise_audit_event(TRACE_HCALLS_MODULE, AUDIT_HCALL_END, 1, &ret_val);
}
#endif
