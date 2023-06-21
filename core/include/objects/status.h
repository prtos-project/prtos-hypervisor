/*
 * FILE: status.h
 *
 * System/partition status
 *
 * www.prtos.org
 */

#ifndef _PRTOS_OBJ_STATUS_H_
#define _PRTOS_OBJ_STATUS_H_

typedef struct {
    prtos_u32_t reset_counter;
    /* Number of HM events emitted. */
    prtos_u64_t num_of_hm_events; /* [[OPTIONAL]] */
    /* Number of HW interrupts received. */
    prtos_u64_t num_of_irqs; /* [[OPTIONAL]] */
    /* Current major cycle interation. */
    prtos_u64_t current_maf; /* [[OPTIONAL]] */
    /* Total number of system messages: */
    prtos_u64_t num_of_sampling_port_msgs_read;    /* [[OPTIONAL]] */
    prtos_u64_t num_of_sampling_port_msgs_written; /* [[OPTIONAL]] */
    prtos_u64_t num_of_queuing_port_msgs_sent;     /* [[OPTIONAL]] */
    prtos_u64_t num_of_queuing_port_msgs_received; /* [[OPTIONAL]] */
} prtos_sys_status_t;

typedef struct {
    prtos_time_t switch_time;
    prtos_s32_t next;
    prtos_s32_t current;
    prtos_s32_t prev;
} prtos_plan_status_t;

typedef struct {
    /* Current state of the partition: ready, suspended ... */
    prtos_u32_t state;
#define PRTOS_STATUS_IDLE 0x0
#define PRTOS_STATUS_READY 0x1
#define PRTOS_STATUS_SUSPENDED 0x2
#define PRTOS_STATUS_HALTED 0x3

    /*By compatibility with ARINC*/
    prtos_u32_t op_mode;
#define PRTOS_OPMODE_IDLE 0x0
#define PRTOS_OPMODE_COLD_RESET 0x1
#define PRTOS_OPMODE_WARM_RESET 0x2
#define PRTOS_OPMODE_NORMAL 0x3

    /* Number of virtual interrupts received. */
    prtos_u64_t num_of_virqs; /* [[OPTIONAL]] */
    /* reset information */
    prtos_u32_t reset_counter;
    prtos_u32_t reset_status;
    prtos_time_t exec_clock;
    /* Total number of partition messages: */
    prtos_u64_t num_of_sampling_port_msgs_read;    /* [[OPTIONAL]] */
    prtos_u64_t num_of_sampling_port_msgs_written; /* [[OPTIONAL]] */
    prtos_u64_t num_of_queuing_port_msgs_sent;     /* [[OPTIONAL]] */
    prtos_u64_t num_of_queuing_port_msgs_received; /* [[OPTIONAL]] */
} prtos_part_status_t;

typedef struct {
    /* Current state of the virtual CPUs: ready, suspended ... */
    prtos_u32_t state;
    //#define PRTOS_STATUS_IDLE 0x0
    //#define PRTOS_STATUS_READY 0x1
    //#define PRTOS_STATUS_SUSPENDED 0x2
    //#define PRTOS_STATUS_HALTED 0x3

    /*Only for debug*/
    prtos_u32_t op_mode;
    //#define PRTOS_OPMODE_IDLE 0x0
    //#define PRTOS_OPMODE_COLD_RESET 0x1
    //#define PRTOS_OPMODE_WARM_RESET 0x2
    //#define PRTOS_OPMODE_NORMAL 0x3
} prtos_virtual_cpu_status_t;

typedef struct {
    prtos_address_t p_addr;
    prtos_u32_t unused : 2, type : 3, counter : 27;
} prtos_phys_page_status_t;

#define PRTOS_GET_SYSTEM_STATUS 0x0
#define PRTOS_GET_SCHED_PLAN_STATUS 0x1
#define PRTOS_SET_PARTITION_OPMODE 0x2
#define PRTOS_GET_PHYSPAGE_STATUS 0x3
#define PRTOS_GET_VCPU_STATUS 0x4

union status_fmd {
    union {
        prtos_sys_status_t system;
        prtos_part_status_t partition;
        prtos_virtual_cpu_status_t vcpu;
        prtos_plan_status_t plan;
        prtos_phys_page_status_t phys_page;
    } status;
    prtos_u32_t op_mode;
};

#ifdef _PRTOS_KERNEL_
extern prtos_sys_status_t system_status;
extern prtos_part_status_t *partition_status;
#endif
#endif
