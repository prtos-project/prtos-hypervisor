/*
 * FILE: prtos_hv.h
 *
 * PRTOS Hypervisor Interface for Linux guests under AArch64 hw-virt.
 *
 * On AArch64, HVC from EL0 (userspace) is undefined. Unlike x86_64 vmcall
 * which traps from any CPL, HVC only works from EL1+. Therefore the
 * hypercall interface is stubbed out in userspace. The virtio demo works
 * in polling mode (shared memory only, no IPVI doorbells).
 *
 * Shared memory addresses are updated for the aarch64 platform layout.
 */

#ifndef _PRTOS_HV_H_
#define _PRTOS_HV_H_

#include <stdint.h>
#include <sys/types.h>

/* ============================================================================
 * PRTOS Hypercall Numbers
 * ============================================================================ */

#define HC_MULTICALL            0
#define HC_HALT_PARTITION       1
#define HC_SUSPEND_PARTITION    2
#define HC_RESUME_PARTITION     3
#define HC_RESET_PARTITION      4
#define HC_SHUTDOWN_PARTITION   5
#define HC_HALT_SYSTEM          6
#define HC_RESET_SYSTEM         7
#define HC_IDLE_SELF            8
#define HC_GET_TIME             9
#define HC_SET_TIMER            10
#define HC_READ_OBJECT          11
#define HC_WRITE_OBJECT         12
#define HC_SEEK_OBJECT          13
#define HC_CTRL_OBJECT          14
#define HC_CLEAR_IRQ_MASK       15
#define HC_SET_IRQ_MASK         16
#define HC_FORCE_IRQS           17
#define HC_CLEAR_IRQS           18
#define HC_ROUTE_IRQ            19
#define HC_UPDATE_PAGE32        20
#define HC_SET_PAGE_TYPE        21
#define HC_INVLD_TLB            22
#define HC_RAISE_IPVI           23
#define HC_RAISE_PARTITION_IPVI 24
#define HC_OVERRIDE_TRAP        25
#define HC_SWITCH_SCHED_PLAN    26
#define HC_GET_GID_BY_NAME      27
#define HC_RESET_VCPU           28
#define HC_HALT_VCPU            29
#define HC_SUSPEND_VCPU         30
#define HC_RESUME_VCPU          31
#define HC_GET_VCPUID           32
#define NR_HYPERCALLS           44

/* ============================================================================
 * PRTOS Return Codes
 * ============================================================================ */

#define PRTOS_OK                 0
#define PRTOS_NO_ACTION         -1
#define PRTOS_UNKNOWN_HYPERCALL -2
#define PRTOS_INVALID_PARAM     -3
#define PRTOS_PERM_ERROR        -4
#define PRTOS_INVALID_CONFIG    -5
#define PRTOS_INVALID_MODE      -6
#define PRTOS_OP_NOT_ALLOWED    -7

/* ============================================================================
 * PRTOS Status Definitions
 * ============================================================================ */

#define PRTOS_STATUS_IDLE       0x0
#define PRTOS_STATUS_READY      0x1
#define PRTOS_STATUS_SUSPENDED  0x2
#define PRTOS_STATUS_HALTED     0x3

#define PRTOS_OPMODE_IDLE       0x0
#define PRTOS_OPMODE_COLD_RESET 0x1
#define PRTOS_OPMODE_WARM_RESET 0x2
#define PRTOS_OPMODE_NORMAL     0x3

#define PRTOS_COLD_RESET        0x0
#define PRTOS_WARM_RESET        0x1

#define PRTOS_GET_SYSTEM_STATUS         0x0
#define PRTOS_GET_SCHED_PLAN_STATUS     0x1
#define PRTOS_SET_PARTITION_OPMODE      0x2
#define PRTOS_GET_PHYSPAGE_STATUS       0x3
#define PRTOS_GET_VCPU_STATUS           0x4

/* ============================================================================
 * Object Descriptor Macros
 * ============================================================================ */

#define OBJ_CLASS_NULL          0
#define OBJ_CLASS_CONSOLE       1
#define OBJ_CLASS_TRACE         2
#define OBJ_CLASS_SAMPLING_PORT 3
#define OBJ_CLASS_QUEUING_PORT  4
#define OBJ_CLASS_MEM           5
#define OBJ_CLASS_HM            6
#define OBJ_CLASS_STATUS        7

#define OBJDESC_BUILD(class, partition_id, id) \
    ((((uint32_t)(class) & 0x7F) << 24) | (((uint32_t)(partition_id) & 0x3FF) << 10) | ((uint32_t)(id) & 0x3FF))

#define PRTOS_HYPERVISOR_ID     0xFF

/* ============================================================================
 * Status Structures
 * ============================================================================ */

typedef int64_t  prtos_time_t;
typedef int32_t  prtos_s32_t;
typedef uint32_t prtos_u32_t;
typedef int64_t  prtos_s64_t;
typedef uint64_t prtos_u64_t;

typedef struct {
    prtos_u32_t reset_counter;
    prtos_u64_t num_of_hm_events;
    prtos_u64_t num_of_irqs;
    prtos_u64_t current_maf;
    prtos_u64_t num_of_sampling_port_msgs_read;
    prtos_u64_t num_of_sampling_port_msgs_written;
    prtos_u64_t num_of_queuing_port_msgs_sent;
    prtos_u64_t num_of_queuing_port_msgs_received;
} prtos_sys_status_t;

typedef struct {
    prtos_time_t switch_time;
    prtos_s32_t next;
    prtos_s32_t current;
    prtos_s32_t prev;
} prtos_plan_status_t;

typedef struct {
    prtos_u32_t state;
    prtos_u32_t op_mode;
    prtos_u64_t num_of_virqs;
    prtos_u32_t reset_counter;
    prtos_u32_t reset_status;
    prtos_time_t exec_clock;
    prtos_u64_t num_of_sampling_port_msgs_read;
    prtos_u64_t num_of_sampling_port_msgs_written;
    prtos_u64_t num_of_queuing_port_msgs_sent;
    prtos_u64_t num_of_queuing_port_msgs_received;
} prtos_part_status_t;

typedef struct {
    prtos_u32_t state;
    prtos_u32_t op_mode;
} prtos_virtual_cpu_status_t;

typedef union {
    union {
        prtos_sys_status_t system;
        prtos_part_status_t partition;
        prtos_virtual_cpu_status_t vcpu;
        prtos_plan_status_t plan;
    } status;
    prtos_u32_t op_mode;
} status_fmd_t;

/* ============================================================================
 * Shared Memory Hypercall Mailbox
 *
 * Uses the last 4KB of the Virtio_Con shared memory region.
 * Updated for aarch64 platform addresses.
 * ============================================================================ */

#define HC_MAILBOX_SHM_BASE     0x20500000UL
#define HC_MAILBOX_SHM_SIZE     0x00040000UL    /* 256KB (Virtio_Con region) */
#define HC_MAILBOX_OFFSET       (HC_MAILBOX_SHM_SIZE - 0x1000)  /* Last 4KB */
#define HC_MAILBOX_GPA          (HC_MAILBOX_SHM_BASE + HC_MAILBOX_OFFSET)

/* ============================================================================
 * Hypervisor Call Interface
 *
 * AArch64 stub: HVC from EL0 (userspace) is not possible. Return -1.
 * The virtio demo operates in polling mode without IPVI doorbells.
 * ============================================================================ */

static inline long prtos_vmcall(long nr, long a0, long a1, long a2, long a3, long a4)
{
    (void)nr; (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    return -1;
}

/* ============================================================================
 * High-level Hypercall Wrappers
 * ============================================================================ */

int prtos_hv_init(void);
int prtos_hv_get_partition_self(void);

int prtos_hv_halt_partition(int partition_id);
int prtos_hv_suspend_partition(int partition_id);
int prtos_hv_resume_partition(int partition_id);
int prtos_hv_reset_partition(int partition_id, int reset_mode, int status);
int prtos_hv_shutdown_partition(int partition_id);

int prtos_hv_get_partition_status(int partition_id, prtos_part_status_t *status);
int prtos_hv_get_system_status(prtos_sys_status_t *status);
int prtos_hv_get_plan_status(prtos_plan_status_t *status);

int prtos_hv_set_plan(int plan_id);

int prtos_hv_halt_system(void);
int prtos_hv_reset_system(int reset_mode);

int prtos_hv_raise_ipvi(int ipvi_no);
int prtos_hv_raise_partition_ipvi(int partition_id, int ipvi_no);

int prtos_hv_write_console(const char *buffer, int length);

int prtos_hv_get_num_partitions(void);

#endif /* _PRTOS_HV_H_ */
