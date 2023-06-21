/*
 * FILE: trace.h
 *
 * Tracing object definitions
 *
 * www.prtos.org
 */

#ifndef _PRTOS_OBJ_TRACE_H_
#define _PRTOS_OBJ_TRACE_H_

#include __PRTOS_INCFLD(linkage.h)

#define PRTOS_TRACE_UNRECOVERABLE 0x3  // This level triggers a health monitoring fault
#define PRTOS_TRACE_WARNING 0x2
#define PRTOS_TRACE_DEBUG 0x1
#define PRTOS_TRACE_NOTIFY 0x0

struct prtos_trace_event {
#define PRTOSTRACE_SIGNATURE 0xc33c
    prtos_u16_t signature;
    prtos_u16_t checksum;
    prtos_u32_t op_code_hi, op_code_lo;

// HIGH
#define TRACE_OPCODE_SEQ_MASK (0xfffffff0 << TRACE_OPCODE_SEQ_BIT)
#define TRACE_OPCODE_SEQ_BIT 4

#define TRACE_OPCODE_CRIT_MASK (0x7 << TRACE_OPCODE_CRIT_BIT)
#define TRACE_OPCODE_CRIT_BIT 1

#define TRACE_OPCODE_SYS_MASK (0x1 << TRACE_OPCODE_SYS_BIT)
#define TRACE_OPCODE_SYS_BIT 0

// LOW
#define TRACE_OPCODE_CODE_MASK (0xffff << TRACE_OPCODE_CODE_BIT)
#define TRACE_OPCODE_CODE_BIT 16

// 256 vcpus
#define TRACE_OPCODE_VCPUID_MASK (0xff << TRACE_OPCODE_VCPUID_BIT)
#define TRACE_OPCODE_VCPUID_BIT 8

// 256 partitions
#define TRACE_OPCODE_PARTID_MASK (0xff << TRACE_OPCODE_PARTID_BIT)
#define TRACE_OPCODE_PARTID_BIT 0
    prtos_time_t timestamp;
#define PRTOSTRACE_PAYLOAD_LENGTH 4
    prtos_word_t payload[PRTOSTRACE_PAYLOAD_LENGTH];
} __PACKED;

typedef struct prtos_trace_event prtos_trace_event_t;

#define PRTOS_TRACE_GET_STATUS 0x0
#define PRTOS_TRACE_LOCK 0x1
#define PRTOS_TRACE_UNLOCK 0x2
#define PRTOS_TRACE_RESET 0x3

typedef struct {
    prtos_s32_t num_of_events;
    prtos_s32_t max_events;
    prtos_s32_t current_event;
} prtos_trace_status_t;

union trace_cmd {
    prtos_trace_status_t status;
};

// Bitmaps
#define TRACE_BM_ALWAYS (~0x0)

#ifdef _PRTOS_KERNEL_
extern prtos_s32_t trace_write_sys_event(prtos_u32_t bitmap, prtos_trace_event_t *event);
#endif
#endif
