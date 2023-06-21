/*
 * FILE: hm.h
 *
 * Health Monitor definitions
 *
 * www.prtos.org
 */

#ifndef _PRTOS_OBJ_HM_H_
#define _PRTOS_OBJ_HM_H_

#include __PRTOS_INCFLD(arch/irqs.h)

struct hm_cpu_ctxt {
    prtos_u32_t pc;
    prtos_u32_t psr;
};

struct prtos_hm_log {
#define PRTOS_HMLOG_SIGNATURE 0xfecf
    prtos_u16_t signature;
    prtos_u16_t checksum;

    prtos_u32_t op_code_hi, op_code_lo;
    // HIGH for op_code_hi
#define HMLOG_OPCODE_SEQ_MASK (0xfffffff << HMLOG_OPCODE_SEQ_BIT)
#define HMLOG_OPCODE_SEQ_BIT 4
    // bits 2 and 3 free

#define HMLOG_OPCODE_VALID_CPUCTXT_MASK (0x1 << HMLOG_OPCODE_VALID_CPUCTXT_BIT)
#define HMLOG_OPCODE_VALID_CPUCTXT_BIT 1
#define HMLOG_OPCODE_SYS_MASK (0x1 << HMLOG_OPCODE_SYS_BIT)
#define HMLOG_OPCODE_SYS_BIT 0

    // LOW bit for op_code_lo
#define HMLOG_OPCODE_EVENT_MASK (0xffff << HMLOG_OPCODE_EVENT_BIT)
#define HMLOG_OPCODE_EVENT_BIT 16

    // 256 vcpus
#define HMLOG_OPCODE_VCPUID_MASK (0xff << HMLOG_OPCODE_VCPUID_BIT)
#define HMLOG_OPCODE_VCPUID_BIT 8

    // 256 partitions
#define HMLOG_OPCODE_PARTID_MASK (0xff << HMLOG_OPCODE_PARTID_BIT)
#define HMLOG_OPCODE_PARTID_BIT 0

    prtos_time_t timestamp;
    union {
#define PRTOS_HMLOG_PAYLOAD_LENGTH 4
        struct hm_cpu_ctxt cpu_ctxt;
        prtos_word_t payload[PRTOS_HMLOG_PAYLOAD_LENGTH];
    };
} __PACKED;

typedef struct prtos_hm_log prtos_hm_log_t;

#define PRTOS_HM_GET_STATUS 0x0
#define PRTOS_HM_LOCK_EVENTS 0x1
#define PRTOS_HM_UNLOCK_EVENTS 0x2
#define PRTOS_HM_RESET_EVENTS 0x3

typedef struct {
    prtos_s32_t num_of_events;
    prtos_s32_t max_events;
    prtos_s32_t current_event;
} prtos_hm_status_t;

union hm_cmd {
    prtos_hm_status_t status;
};

/*Value of the reset status in PCT when the partition is reset via HM*/
#define PRTOS_RESET_STATUS_PARTITION_NORMAL_START 0
#define PRTOS_RESET_STATUS_PARTITION_RESTART 1
#define PRTOS_HM_RESET_STATUS_MODULE_RESTART 2
#define PRTOS_HM_RESET_STATUS_PARTITION_RESTART 3
#define PRTOS_HM_RESET_STATUS_USER_CODE_BIT 16
#define PRTOS_HM_RESET_STATUS_EVENT_MASK 0xffff

#ifdef _PRTOS_KERNEL_
#include <arch/asm.h>
#include <stdc.h>

extern prtos_s32_t hm_raise_event(prtos_hm_log_t *log);
static inline void raise_hm_part_event(prtos_u32_t event_id, prtos_id_t part_id, prtos_id_t vcpu_id, prtos_u32_t system) {
    prtos_hm_log_t hm_log;
    memset(&hm_log, 0, sizeof(prtos_hm_log_t));
    hm_log.op_code_lo = (event_id << HMLOG_OPCODE_EVENT_BIT) | (part_id << HMLOG_OPCODE_PARTID_BIT) | (vcpu_id << HMLOG_OPCODE_VCPUID_BIT);
    hm_log.op_code_hi = system ? HMLOG_OPCODE_SYS_MASK : 0;
    hm_raise_event(&hm_log);
}

#endif
#endif
