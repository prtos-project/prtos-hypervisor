/*
 * FILE: trace.c
 *
 * Tracing mechanism
 *
 * www.prtos.org
 */

#include <assert.h>
#include <rsvmem.h>
#include <boot.h>
#include <checksum.h>
#include <hypercalls.h>
#include <kthread.h>
#include <kdevice.h>
#include <stdc.h>
#include <prtosconf.h>
#include <sched.h>
#include <objects/trace.h>
#include <objects/hm.h>
#include <logstream.h>

static struct log_stream *trace_log_stream, prtos_trace_log_stream;
static prtos_u32_t seq = 0;

static prtos_s32_t read_trace(prtos_obj_desc_t desc, prtos_trace_event_t *__g_param event, prtos_u32_t size) {
    prtos_s32_t e, num_of_traces = size / sizeof(prtos_trace_event_t);
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct log_stream *log;
    prtos_id_t part_id;

    part_id = OBJDESC_GET_PARTITIONID(desc);
    if (part_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id))
        if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if (!num_of_traces) return PRTOS_INVALID_PARAM;

    if (check_gp_aram(event, size, 4, PFLAG_NOT_NULL | PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;

    if (part_id == PRTOS_HYPERVISOR_ID)
        log = &prtos_trace_log_stream;
    else {
        if ((part_id < 0) || (part_id >= prtos_conf_table.num_of_partitions)) return PRTOS_INVALID_PARAM;
        log = &trace_log_stream[part_id];
    }

    for (e = 0; e < num_of_traces; e++)
        if (log_stream_get(log, &event[e]) < 0) return e * sizeof(prtos_trace_event_t);

    return num_of_traces * sizeof(prtos_trace_event_t);
}

static prtos_s32_t write_trace(prtos_obj_desc_t desc, prtos_trace_event_t *__g_param event, prtos_u32_t size, prtos_u32_t *bitmap) {
    prtos_s32_t e, num_of_traces = size / sizeof(prtos_trace_event_t), written;
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct log_stream *log;
    prtos_id_t part_id;
    part_id = OBJDESC_GET_PARTITIONID(desc);
    if (part_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id)) return PRTOS_PERM_ERROR;

    if (!num_of_traces) return PRTOS_INVALID_PARAM;

    if (check_gp_aram(event, size, 4, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;
    if (check_gp_aram(bitmap, sizeof(prtos_u32_t), 4, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;
    log = &trace_log_stream[part_id];
    for (written = 0, e = 0; e < num_of_traces; e++) {
        if (bitmap && (get_partition(info->sched.current_kthread)->cfg->trace.bitmap & *bitmap)) {
            prtos_u32_t tmp_seq;
            event[e].signature = PRTOSTRACE_SIGNATURE;
            event[e].op_code_lo &= ~TRACE_OPCODE_PARTID_MASK;
            event[e].op_code_lo |= (KID2PARTID(info->sched.current_kthread->ctrl.g->id) << TRACE_OPCODE_PARTID_BIT) & TRACE_OPCODE_PARTID_MASK;
            event[e].op_code_lo &= ~TRACE_OPCODE_VCPUID_MASK;
            event[e].op_code_lo |= (KID2VCPUID(info->sched.current_kthread->ctrl.g->id) << TRACE_OPCODE_VCPUID_BIT) & TRACE_OPCODE_VCPUID_MASK;
            event[e].timestamp = get_sys_clock_usec();
            tmp_seq = seq++;
            event[e].op_code_hi &= ~TRACE_OPCODE_SEQ_MASK;
            event[e].op_code_hi |= tmp_seq << TRACE_OPCODE_SEQ_BIT;
            event[e].checksum = 0;
            event[e].checksum = calc_check_sum((prtos_u16_t *)&event[e], sizeof(struct prtos_trace_event));
            log_stream_insert(log, &event[e]);
            written++;
        }
        if (((event->op_code_hi & TRACE_OPCODE_CRIT_MASK) >> TRACE_OPCODE_CRIT_BIT) == PRTOS_TRACE_UNRECOVERABLE) {
            prtos_hm_log_t log;
            log.op_code_lo = (KID2PARTID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_PARTID_BIT) |
                             (KID2VCPUID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_VCPUID_BIT) |
                             (PRTOS_HM_EV_PARTITION_ERROR << HMLOG_OPCODE_EVENT_BIT);
            memcpy(log.payload, event->payload, sizeof(prtos_word_t) * PRTOS_HMLOG_PAYLOAD_LENGTH - 1);

            hm_raise_event(&log);
        }
    }

    return written * sizeof(prtos_trace_event_t);
}

static prtos_s32_t seek_trace(prtos_obj_desc_t desc, prtos_u32_t offset, prtos_u32_t whence) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct log_stream *log;
    prtos_id_t part_id;

    part_id = OBJDESC_GET_PARTITIONID(desc);
    if (part_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id))
        if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if (part_id == PRTOS_HYPERVISOR_ID)
        log = &prtos_trace_log_stream;
    else {
        if ((part_id < 0) || (part_id >= prtos_conf_table.num_of_partitions)) return PRTOS_INVALID_PARAM;
        log = &trace_log_stream[part_id];
    }

    return log_stream_seek(log, offset, whence);
}

static prtos_s32_t ctrl_trace(prtos_obj_desc_t desc, prtos_u32_t cmd, union trace_cmd *__g_param args) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct log_stream *log;
    prtos_id_t part_id;

    part_id = OBJDESC_GET_PARTITIONID(desc);
    if (part_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id))
        if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;
    if (check_gp_aram(args, sizeof(union trace_cmd), 4, PFLAG_NOT_NULL | PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;
    if (part_id == PRTOS_HYPERVISOR_ID)
        log = &prtos_trace_log_stream;
    else {
        if ((part_id < 0) || (part_id >= prtos_conf_table.num_of_partitions)) return PRTOS_INVALID_PARAM;
        log = &trace_log_stream[part_id];
    }

    switch (cmd) {
        case PRTOS_TRACE_GET_STATUS:
            args->status.num_of_events = log->ctrl.elem;
            args->status.max_events = log->info.max_num_of_elem;
            args->status.current_event = log->ctrl.d;
            return PRTOS_OK;
        case PRTOS_TRACE_LOCK:
            log_stream_lock(log);
            return PRTOS_OK;
        case PRTOS_TRACE_UNLOCK:
            log_stream_unlock(log);
            return PRTOS_OK;
        case PRTOS_TRACE_RESET:
            if (part_id == PRTOS_HYPERVISOR_ID)
                log_stream_init(log, find_kdev(&prtos_conf_table.hpv.trace.dev), sizeof(prtos_trace_event_t));
            else
                log_stream_init(log, find_kdev(&prtos_conf_partition_table[part_id].trace.dev), sizeof(prtos_trace_event_t));
            return PRTOS_OK;
    }
    return PRTOS_INVALID_PARAM;
}

static const struct object trace_obj = {
    .read = (read_obj_op_t)read_trace,
    .write = (write_obj_op_t)write_trace,
    .seek = (seek_obj_op_t)seek_trace,
    .ctrl = (ctrl_obj_op_t)ctrl_trace,
};

prtos_s32_t __VBOOT setup_trace(void) {
    prtos_s32_t e;
    GET_MEMZ(trace_log_stream, sizeof(struct log_stream) * prtos_conf_table.num_of_partitions);
    log_stream_init(&prtos_trace_log_stream, find_kdev(&prtos_conf_table.hpv.trace.dev), sizeof(prtos_trace_event_t));

    for (e = 0; e < prtos_conf_table.num_of_partitions; e++)
        log_stream_init(&trace_log_stream[e], find_kdev(&prtos_conf_partition_table[e].trace.dev), sizeof(prtos_trace_event_t));

    object_table[OBJ_CLASS_TRACE] = &trace_obj;

    return 0;
}

prtos_s32_t trace_write_sys_event(prtos_u32_t bitmap, prtos_trace_event_t *event) {
    ASSERT(event);

    if (prtos_conf_table.hpv.trace.bitmap & bitmap) {
        prtos_u32_t tmp_seq;
        event->signature = PRTOSTRACE_SIGNATURE;
        event->timestamp = get_sys_clock_usec();

        tmp_seq = seq++;
        event->op_code_hi &= ~TRACE_OPCODE_SEQ_MASK;
        event->op_code_hi |= tmp_seq << TRACE_OPCODE_SEQ_BIT;

        event->checksum = 0;
        event->checksum = calc_check_sum((prtos_u16_t *)event, sizeof(struct prtos_trace_event));
        return log_stream_insert(&prtos_trace_log_stream, event);
    }

    return -2;
}

REGISTER_OBJ(setup_trace);

#ifdef CONFIG_AUDIT_EVENTS
prtos_s32_t is_audit_event_masked(prtos_u32_t module) {
    if (prtos_conf_table.hpv.trace.bitmap & (1 << module)) return 1;
    return 0;
}

void raise_audit_event(prtos_u32_t module, prtos_u32_t event, prtos_s32_t payload_len, prtos_word_t *payload) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_trace_event_t trace;
    ASSERT((module >= 0) && (module < TRACE_NO_MODULES));
    ASSERT(payload_len <= PRTOSTRACE_PAYLOAD_LENGTH);
    if (prtos_conf_table.hpv.trace.bitmap & (1 << module)) {
        prtos_id_t part_id, vcpu_id;
        if (info->sched.current_kthread->ctrl.g) {
            part_id = KID2PARTID(info->sched.current_kthread->ctrl.g->id);
            vcpu_id = KID2VCPUID(info->sched.current_kthread->ctrl.g->id);
        } else {
            part_id = -1;
            vcpu_id = -1;
        }

        trace.op_code_lo = ((((module << 8) | event) << TRACE_OPCODE_CODE_BIT) & TRACE_OPCODE_CODE_MASK) |
                           ((part_id << TRACE_OPCODE_PARTID_BIT) & TRACE_OPCODE_PARTID_MASK) |
                           ((vcpu_id << TRACE_OPCODE_VCPUID_BIT) & TRACE_OPCODE_VCPUID_MASK);

        trace.op_code_hi = PRTOS_TRACE_NOTIFY << TRACE_OPCODE_CRIT_BIT | TRACE_OPCODE_SYS_MASK;

        if (payload_len) memcpy(trace.payload, payload, sizeof(prtos_u32_t) * payload_len);
        trace_write_sys_event((1 << module), &trace);
    }
}
#endif
