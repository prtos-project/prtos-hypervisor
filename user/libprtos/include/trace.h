/*
 * FILE: trace.h
 *
 * tracing subsystem
 *
 * www.prtos.org
 */

#ifndef _LIB_PRTOS_TRACE_H_
#define _LIB_PRTOS_TRACE_H_

#include <prtos_inc/config.h>
#include <prtos_inc/objdir.h>
#include <prtos_inc/objects/trace.h>

extern prtos_s32_t prtos_trace_event(prtos_u32_t bitmask, prtos_trace_event_t *event);
extern prtos_s32_t prtos_trace_open(prtos_id_t id);
extern prtos_s32_t prtos_trace_read(prtos_s32_t trace_stream, prtos_trace_event_t *trace_event_ptr);
extern prtos_s32_t prtos_trace_seek(prtos_s32_t trace_stream, prtos_s32_t offset, prtos_u32_t whence);
#define PRTOS_TRACE_SEEK_CUR PRTOS_OBJ_SEEK_CUR
#define PRTOS_TRACE_SEEK_END PRTOS_OBJ_SEEK_END
#define PRTOS_TRACE_SEEK_SET PRTOS_OBJ_SEEK_SET
extern prtos_s32_t prtos_trace_status(prtos_s32_t trace_stream, prtos_trace_status_t *trace_status_ptr);

#endif
