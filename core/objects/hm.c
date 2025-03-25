/*
 * FILE: hm.c
 *
 * Health Monitor
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <checksum.h>
#include <kthread.h>
#include <hypercalls.h>
#include <logstream.h>
#include <rsvmem.h>
#include <stdc.h>
#include <prtosconf.h>
#include <sched.h>
#include <objects/hm.h>
#ifdef CONFIG_OBJ_STATUS_ACC
#include <objects/status.h>
#endif

extern prtos_u32_t reset_status_init[];

static struct log_stream hm_log_stream;
static prtos_s32_t hm_init = 0;
static prtos_u32_t seq = 0;

static prtos_s32_t read_hm_log(prtos_obj_desc_t desc, prtos_hm_log_t *__g_param log, prtos_u32_t size) {
    prtos_s32_t e, num_of_logs = size / sizeof(prtos_hm_log_t);
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    if (OBJDESC_GET_PARTITIONID(desc) != PRTOS_HYPERVISOR_ID) return PRTOS_INVALID_PARAM;

    if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if (check_gp_param(log, size, 4, PFLAG_NOT_NULL | PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;

    if (!log || !num_of_logs) return PRTOS_INVALID_PARAM;

    for (e = 0; e < num_of_logs; e++)
        if (log_stream_get(&hm_log_stream, &log[e]) < 0) return e * sizeof(prtos_hm_log_t);

    return num_of_logs * sizeof(prtos_hm_log_t);
}

static prtos_s32_t seek_hm_log(prtos_obj_desc_t desc, prtos_u32_t offset, prtos_u32_t whence) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    if (OBJDESC_GET_PARTITIONID(desc) != PRTOS_HYPERVISOR_ID) return PRTOS_INVALID_PARAM;

    if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if (log_stream_seek(&hm_log_stream, offset, whence) < 0) return PRTOS_INVALID_PARAM;
    return PRTOS_OK;
}

static prtos_s32_t ctrl_hm_log(prtos_obj_desc_t desc, prtos_u32_t cmd, union hm_cmd *__g_param args) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    if (OBJDESC_GET_PARTITIONID(desc) != PRTOS_HYPERVISOR_ID) return PRTOS_INVALID_PARAM;

    if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;
    if (check_gp_param(args, sizeof(union hm_cmd), 4, PFLAG_NOT_NULL | PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;
    switch (cmd) {
        case PRTOS_HM_GET_STATUS:
            args->status.num_of_events = hm_log_stream.ctrl.elem;
            args->status.max_events = hm_log_stream.info.max_num_of_elem;
            args->status.current_event = hm_log_stream.ctrl.d;
            return PRTOS_OK;
        case PRTOS_HM_LOCK_EVENTS:
            log_stream_lock(&hm_log_stream);
            return PRTOS_OK;
        case PRTOS_HM_UNLOCK_EVENTS:
            log_stream_unlock(&hm_log_stream);
            return PRTOS_OK;
        case PRTOS_HM_RESET_EVENTS:
            log_stream_init(&hm_log_stream, find_kdev(&prtos_conf_table.hpv.hm_device), sizeof(prtos_hm_log_t));
            return PRTOS_OK;
    }
    return PRTOS_INVALID_PARAM;
}

static const struct object hm_obj = {
    .read = (read_obj_op_t)read_hm_log,
    .seek = (seek_obj_op_t)seek_hm_log,
    .ctrl = (ctrl_obj_op_t)ctrl_hm_log,
};

prtos_s32_t __VBOOT setup_hm(void) {
    log_stream_init(&hm_log_stream, find_kdev(&prtos_conf_table.hpv.hm_device), sizeof(prtos_hm_log_t));
    object_table[OBJ_CLASS_HM] = &hm_obj;
    hm_init = 1;
    return 0;
}

#ifdef CONFIG_OBJ_HM_VERBOSE
prtos_s8_t *hm_event_to_str[PRTOS_HM_MAX_EVENTS] = {
    FILL_TAB(PRTOS_HM_EV_FATAL_ERROR),
    FILL_TAB(PRTOS_HM_EV_SYSTEM_ERROR),
    FILL_TAB(PRTOS_HM_EV_PARTITION_ERROR),
    FILL_TAB(PRTOS_HM_EV_FP_ERROR),
    FILL_TAB(PRTOS_HM_EV_MEM_PROTECTION),
    FILL_TAB(PRTOS_HM_EV_UNEXPECTED_TRAP),
#ifdef CONFIG_x86
    FILL_TAB(PRTOS_HM_EV_X86_DIVIDE_ERROR),
    FILL_TAB(PRTOS_HM_EV_X86_DEBUG),
    FILL_TAB(PRTOS_HM_EV_X86_NMI_INTERRUPT),
    FILL_TAB(PRTOS_HM_EV_X86_BREAKPOINT),
    FILL_TAB(PRTOS_HM_EV_X86_OVERFLOW),
    FILL_TAB(PRTOS_HM_EV_X86_BOUND_RANGE_EXCEEDED),
    FILL_TAB(PRTOS_HM_EV_X86_INVALID_OPCODE),
    FILL_TAB(PRTOS_HM_EV_X86_DEVICE_NOT_AVAILABLE),
    FILL_TAB(PRTOS_HM_EV_X86_DOUBLE_FAULT),
    FILL_TAB(PRTOS_HM_EV_X86_COPROCESSOR_SEGMENT_OVERRUN),
    FILL_TAB(PRTOS_HM_EV_X86_INVALID_TSS),
    FILL_TAB(PRTOS_HM_EV_X86_SEGMENT_NOT_PRESENT),
    FILL_TAB(PRTOS_HM_EV_X86_STACK_FAULT),
    FILL_TAB(PRTOS_HM_EV_X86_GENERAL_PROTECTION),
    FILL_TAB(PRTOS_HM_EV_X86_PAGE_FAULT),
    FILL_TAB(PRTOS_HM_EV_X86_X87_FPU_ERROR),
    FILL_TAB(PRTOS_HM_EV_X86_ALIGNMENT_CHECK),
    FILL_TAB(PRTOS_HM_EV_X86_MACHINE_CHECK),
    FILL_TAB(PRTOS_HM_EV_X86_SIMD_FLOATING_POINT),
#endif
};

prtos_s8_t *hm_gog_to_str[2] = {
    FILL_TAB(PRTOS_HM_LOG_DISABLED),
    FILL_TAB(PRTOS_HM_LOG_ENABLED),
};

prtos_s8_t *hm_action_to_str[PRTOS_HM_MAX_ACTIONS] = {
    FILL_TAB(PRTOS_HM_AC_IGNORE),
    FILL_TAB(PRTOS_HM_AC_SHUTDOWN),
    FILL_TAB(PRTOS_HM_AC_PARTITION_COLD_RESET),
    FILL_TAB(PRTOS_HM_AC_PARTITION_WARM_RESET),
    FILL_TAB(PRTOS_HM_AC_HYPERVISOR_COLD_RESET),
    FILL_TAB(PRTOS_HM_AC_HYPERVISOR_WARM_RESET),
    FILL_TAB(PRTOS_HM_AC_SUSPEND),
    FILL_TAB(PRTOS_HM_AC_PARTITION_HALT),
    FILL_TAB(PRTOS_HM_AC_HYPERVISOR_HALT),
    FILL_TAB(PRTOS_HM_AC_PROPAGATE),
    FILL_TAB(PRTOS_HM_AC_SWITCH_TO_MAINTENANCE),
};

#endif

REGISTER_OBJ(setup_hm);

prtos_s32_t hm_raise_event(prtos_hm_log_t *log) {
    prtos_s32_t propagate = 0;
    prtos_s32_t old_plan_id;
    cpu_ctxt_t ctxt;
    prtos_u32_t event_id, system;
    prtos_id_t partition_id;
    prtos_time_t current_time;
#ifdef CONFIG_AUDIT_EVENTS
    prtos_word_t audit_args[3];
#endif
#ifdef CONFIG_OBJ_HM_VERBOSE
    prtos_s32_t e;
#endif

    if (!hm_init) return 0;
#ifdef CONFIG_AUDIT_EVENTS
    audit_args[0] = log->op_code_lo;
    audit_args[1] = log->op_code_hi;
    audit_args[2] = log->cpu_ctxt.pc;
    raise_audit_event(TRACE_HM_MODULE, AUDIT_HM_EVENT_RAISED, 3, audit_args);
#endif
    current_time = get_sys_clock_usec();
    log->signature = PRTOS_HMLOG_SIGNATURE;
    event_id = (log->op_code_lo & HMLOG_OPCODE_EVENT_MASK) >> HMLOG_OPCODE_EVENT_BIT;
    ASSERT((event_id >= 0) && (event_id < PRTOS_HM_MAX_EVENTS));
    partition_id = (log->op_code_lo & HMLOG_OPCODE_PARTID_MASK) >> HMLOG_OPCODE_PARTID_BIT;
    system = (log->op_code_hi & HMLOG_OPCODE_SYS_MASK) ? 1 : 0;
#ifdef CONFIG_OBJ_STATUS_ACC
    system_status.num_of_hm_events++;
#endif
    log->timestamp = current_time;
#ifdef CONFIG_OBJ_HM_VERBOSE
    kprintf("[HM] %lld:", log->timestamp);

    if ((event_id < PRTOS_HM_MAX_EVENTS) && hm_event_to_str[event_id])
        kprintf("%s ", hm_event_to_str[event_id]);
    else
        kprintf("unknown ");

    kprintf("(%d)", event_id);

    if (system)
        kprintf(":SYS");
    else
        kprintf(":PART");

    kprintf("(%d)\n", partition_id);

    if (!(log->op_code_hi & HMLOG_OPCODE_VALID_CPUCTXT_MASK)) {
        for (e = 0; e < PRTOS_HMLOG_PAYLOAD_LENGTH; e++) kprintf("0x%lx ", log->payload[e]);
        kprintf("\n");
    } else
        print_hm_cpu_ctxt(&log->cpu_ctxt);
#endif
    if (system) {
#ifdef CONFIG_OBJ_HM_VERBOSE
        kprintf("[HM] ");
        if ((prtos_conf_table.hpv.hm_table[event_id].action < PRTOS_HM_MAX_ACTIONS) && hm_action_to_str[prtos_conf_table.hpv.hm_table[event_id].action])
            kprintf("%s", hm_action_to_str[prtos_conf_table.hpv.hm_table[event_id].action]);
        else
            kprintf("unknown");
        kprintf("(%d) ", prtos_conf_table.hpv.hm_table[event_id].action);

        kprintf("%s\n", hm_gog_to_str[prtos_conf_table.hpv.hm_table[event_id].log]);
#endif
#ifdef CONFIG_AUDIT_EVENTS
        audit_args[0] = ((prtos_conf_table.hpv.hm_table[event_id].log) ? 1 << 31 : 0) | prtos_conf_table.hpv.hm_table[event_id].action;
        raise_audit_event(TRACE_HM_MODULE, AUDIT_HM_HPV_ACTION, 1, audit_args);
#endif
        if (prtos_conf_table.hpv.hm_table[event_id].log) {
            prtos_u32_t tmp_seq;
            log->checksum = 0;
            tmp_seq = seq++;
            log->op_code_hi &= ~HMLOG_OPCODE_SEQ_MASK;
            log->op_code_hi |= tmp_seq << HMLOG_OPCODE_SEQ_BIT;
            log->checksum = calc_check_sum((prtos_u16_t *)log, sizeof(struct prtos_hm_log));
            log_stream_insert(&hm_log_stream, log);
        }
        switch (prtos_conf_table.hpv.hm_table[event_id].action) {
            case PRTOS_HM_AC_IGNORE:
                // Doing nothing
                break;
            case PRTOS_HM_AC_HYPERVISOR_COLD_RESET:
                reset_status_init[0] =
                    (PRTOS_HM_RESET_STATUS_MODULE_RESTART << PRTOS_HM_RESET_STATUS_USER_CODE_BIT) | (event_id & PRTOS_HM_RESET_STATUS_EVENT_MASK);
                reset_system(PRTOS_COLD_RESET);
                break;
            case PRTOS_HM_AC_HYPERVISOR_WARM_RESET:
                reset_status_init[0] =
                    (PRTOS_HM_RESET_STATUS_MODULE_RESTART << PRTOS_HM_RESET_STATUS_USER_CODE_BIT) | (event_id & PRTOS_HM_RESET_STATUS_EVENT_MASK);
                reset_system(PRTOS_WARM_RESET);
                break;
            case PRTOS_HM_AC_SWITCH_TO_MAINTENANCE:
                switch_sched_plan(1, &old_plan_id);
                make_plan_switch(current_time, GET_LOCAL_PROCESSOR()->sched.data);
                schedule();
                break;
            case PRTOS_HM_AC_HYPERVISOR_HALT:
                halt_system();
                break;
            default:
                get_cpu_ctxt(&ctxt);
                system_panic(&ctxt, "Unknown health-monitor action %d\n", prtos_conf_table.hpv.hm_table[event_id].action);
        }
    } else {
#ifdef CONFIG_OBJ_HM_VERBOSE
        kprintf("[HM] ");
        if ((partition_table[partition_id].cfg->hm_table[event_id].action < PRTOS_HM_MAX_ACTIONS) &&
            hm_action_to_str[partition_table[partition_id].cfg->hm_table[event_id].action])
            kprintf("%s", hm_action_to_str[partition_table[partition_id].cfg->hm_table[event_id].action]);
        else
            kprintf("unknown");

        kprintf("(%d) ", partition_table[partition_id].cfg->hm_table[event_id].action);
        kprintf("%s\n", hm_gog_to_str[partition_table[partition_id].cfg->hm_table[event_id].log]);
#endif
#ifdef CONFIG_AUDIT_EVENTS
        audit_args[0] =
            ((partition_table[partition_id].cfg->hm_table[event_id].log) ? 1 << 31 : 0) | partition_table[partition_id].cfg->hm_table[event_id].action;
        raise_audit_event(TRACE_HM_MODULE, AUDIT_HM_PART_ACTION, 1, audit_args);
#endif
        if (partition_table[partition_id].cfg->hm_table[event_id].log) {
            prtos_u32_t tmp_seq;
            log->checksum = 0;
            tmp_seq = seq++;
            log->op_code_hi &= ~HMLOG_OPCODE_SEQ_MASK;
            log->op_code_hi |= tmp_seq << HMLOG_OPCODE_SEQ_BIT;
            log->checksum = calc_check_sum((prtos_u16_t *)log, sizeof(struct prtos_hm_log));
            log_stream_insert(&hm_log_stream, log);
        }

        ASSERT(partition_id < prtos_conf_table.num_of_partitions);
        switch (partition_table[partition_id].cfg->hm_table[event_id].action) {
            case PRTOS_HM_AC_IGNORE:
                // Doing nothing
                break;
            case PRTOS_HM_AC_SHUTDOWN:
                SHUTDOWN_PARTITION(partition_id);
                break;
            case PRTOS_HM_AC_PARTITION_COLD_RESET:
#ifdef CONFIG_OBJ_HM_VERBOSE
                kprintf("[HM] Partition %d cold reseted\n", partition_id);
#endif
                if (reset_partition(&partition_table[partition_id], PRTOS_COLD_RESET, event_id) < 0) {
                    HALT_PARTITION(partition_id);
                    schedule();
                }

                break;
            case PRTOS_HM_AC_PARTITION_WARM_RESET:
#ifdef CONFIG_OBJ_HM_VERBOSE
                kprintf("[HM] Partition %d warm reseted\n", partition_id);
#endif
                if (reset_partition(&partition_table[partition_id], PRTOS_WARM_RESET, event_id) < 0) {
                    HALT_PARTITION(partition_id);
                    schedule();
                }

                break;
            case PRTOS_HM_AC_HYPERVISOR_COLD_RESET:
                reset_status_init[0] =
                    (PRTOS_HM_RESET_STATUS_MODULE_RESTART << PRTOS_HM_RESET_STATUS_USER_CODE_BIT) | (event_id & PRTOS_HM_RESET_STATUS_EVENT_MASK);
                reset_system(PRTOS_COLD_RESET);
                break;
            case PRTOS_HM_AC_HYPERVISOR_WARM_RESET:
                reset_status_init[0] =
                    (PRTOS_HM_RESET_STATUS_MODULE_RESTART << PRTOS_HM_RESET_STATUS_USER_CODE_BIT) | (event_id & PRTOS_HM_RESET_STATUS_EVENT_MASK);
                reset_system(PRTOS_WARM_RESET);
                break;
            case PRTOS_HM_AC_SUSPEND:
                ASSERT(partition_id != PRTOS_HYPERVISOR_ID);
#ifdef CONFIG_OBJ_HM_VERBOSE
                kprintf("[HM] Partition %d suspended\n", partition_id);
#endif
                SUSPEND_PARTITION(partition_id);
                schedule();
                break;
            case PRTOS_HM_AC_PARTITION_HALT:
                ASSERT(partition_id != PRTOS_HYPERVISOR_ID);
#ifdef CONFIG_OBJ_HM_VERBOSE
                kprintf("[HM] Partition %d halted\n", partition_id);
#endif
                HALT_PARTITION(partition_id);
                schedule();
                break;
            case PRTOS_HM_AC_HYPERVISOR_HALT:
                halt_system();
                break;
            case PRTOS_HM_AC_SWITCH_TO_MAINTENANCE:
                switch_sched_plan(1, &old_plan_id);
                make_plan_switch(current_time, GET_LOCAL_PROCESSOR()->sched.data);
                schedule();
                break;
            case PRTOS_HM_AC_PROPAGATE:
                propagate = 1;
                break;
            default:
                get_cpu_ctxt(&ctxt);
                system_panic(&ctxt, "Unknown health-monitor action %d\n", partition_table[partition_id].cfg->hm_table[event_id].action);
        }
    }

    return propagate;
}
