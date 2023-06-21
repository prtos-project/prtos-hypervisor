/*
 * FILE: trace.c
 *
 * Tracing functionality
 *
 * www.prtos.org
 */

#include <prtos.h>
#include <prtos_inc/objdir.h>
#include <prtos_inc/objects/trace.h>

prtos_s32_t prtos_trace_open(prtos_id_t id) {
    prtos_s32_t num_of_partitions;
    if (id != PRTOS_PARTITION_SELF)
        if (!(lib_prtos_params.part_ctrl_table[prtos_get_vcpuid()]->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    num_of_partitions = (lib_prtos_params.part_ctrl_table[prtos_get_vcpuid()]->flags >> 16) & 0xff;
    if ((id != PRTOS_HYPERVISOR_ID) && ((id < 0) || (id >= num_of_partitions))) return PRTOS_INVALID_PARAM;
    return OBJDESC_BUILD(OBJ_CLASS_TRACE, id, 0);
}

prtos_s32_t prtos_trace_event(prtos_u32_t bitmask, prtos_trace_event_t *event) {
    if (!event) return PRTOS_INVALID_PARAM;
    if (prtos_write_object(OBJDESC_BUILD(OBJ_CLASS_TRACE, PRTOS_PARTITION_SELF, 0), event, sizeof(prtos_trace_event_t), &bitmask) < sizeof(prtos_trace_event_t))
        return PRTOS_INVALID_CONFIG;
    return PRTOS_OK;
}

prtos_s32_t prtos_trace_read(prtos_s32_t trace_stream, prtos_trace_event_t *trace_event_ptr) {
    prtos_s32_t ret;
    if (OBJDESC_GET_CLASS(trace_stream) != OBJ_CLASS_TRACE) return PRTOS_INVALID_PARAM;
    if (!trace_event_ptr) return PRTOS_INVALID_PARAM;

    ret = prtos_read_object(trace_stream, trace_event_ptr, sizeof(prtos_trace_event_t), 0);
    return (ret > 0) ? (ret / sizeof(prtos_trace_event_t)) : ret;
}

prtos_s32_t prtos_trace_seek(prtos_s32_t trace_stream, prtos_s32_t offset, prtos_u32_t whence) {
    if (OBJDESC_GET_CLASS(trace_stream) != OBJ_CLASS_TRACE) return PRTOS_INVALID_PARAM;

    return prtos_seek_object(trace_stream, offset, whence);
}

prtos_s32_t prtos_trace_status(prtos_s32_t trace_stream, prtos_trace_status_t *trace_status_ptr) {
    if (OBJDESC_GET_CLASS(trace_stream) != OBJ_CLASS_TRACE) return PRTOS_INVALID_PARAM;

    if (!trace_status_ptr) return PRTOS_INVALID_PARAM;
    return prtos_ctrl_object(trace_stream, PRTOS_TRACE_GET_STATUS, trace_status_ptr);
}
