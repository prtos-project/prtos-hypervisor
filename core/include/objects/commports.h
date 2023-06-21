/*
 * FILE: commports.h
 *
 * Communication port object definitions
 *
 * www.prtos.org
 */

#ifndef _PRTOS_OBJ_COMMPORTS_H_
#define _PRTOS_OBJ_COMMPORTS_H_
// Commands
#define PRTOS_COMM_CREATE_PORT 0x0
#define PRTOS_COMM_GET_PORT_STATUS 0x1
#define PRTOS_COMM_GET_PORT_INFO 0x2

#ifdef _PRTOS_KERNEL_
#include <kthread.h>
#include <spinlock.h>
#include <kdevice.h>
#else
#ifndef __g_param
#define __g_param
#endif
#endif

typedef struct {
    prtos_time_t timestamp;
    prtos_u32_t last_msg_size;
    prtos_u32_t flags;
#define PRTOS_COMM_PORT_STATUS_B 1
#define PRTOS_COMM_PORT_STATUS_M 0x3
#define PRTOS_COMM_EMPTY_PORT 0x0
#define PRTOS_COMM_CONSUMED_MSG (1 << PRTOS_COMM_PORT_STATUS_B)
#define PRTOS_COMM_NEW_MSG (2 << PRTOS_COMM_PORT_STATUS_B)
} prtos_sampling_port_status_t;

typedef struct {
    char *__g_param port_name;
#define PRTOS_INFINITE_TIME ((prtos_u32_t)-1)
    prtos_time_t valid_period;  // Refresh period.
    prtos_u32_t max_msg_size;   // Max message size.
    prtos_u32_t direction;
} prtos_sampling_port_info_t;

typedef struct {
    prtos_u32_t num_of_msgs;  // Current number of messages.
} prtos_queuing_port_status_t;

typedef struct {
    char *__g_param port_name;
    prtos_u32_t max_msg_size;     // Max message size.
    prtos_u32_t max_num_of_msgs;  // Max number of messages.
    prtos_u32_t direction;
    prtos_time_t valid_period;
} prtos_queuing_port_info_t;

union sampling_port_cmd {
    struct create_s_cmd {
        char *__g_param port_name;
        prtos_u32_t max_msg_size;
        prtos_u32_t direction;
        prtos_time_t valid_period;
    } create;
    prtos_sampling_port_status_t status;
    prtos_sampling_port_info_t info;
};

union queuing_port_cmd {
    struct create_q_cmd {
        char *__g_param port_name;
        prtos_u32_t max_num_of_msgs;
        prtos_u32_t max_msg_size;
        prtos_u32_t direction;
    } create;
    prtos_queuing_port_status_t status;
    prtos_queuing_port_info_t info;
};

#define PRTOS_COMM_MSG_VALID 0x1

#ifdef _PRTOS_KERNEL_

union channel {
    struct {
        char *buffer;
        prtos_s32_t length;
        prtos_time_t timestamp;
        partition_t **receiver_table;
        prtos_s32_t *receiver_port_table;
        prtos_s32_t num_of_receivers;
        partition_t *sender;
        prtos_s32_t sender_port;
        spin_lock_t lock;
    } s;

    struct {
        struct msg {
            struct dyn_list_node list_node;
            char *buffer;
            prtos_s32_t length;
            prtos_time_t timestamp;
        } * msg_pool;
        struct dyn_list free_msgs, recv_msgs;
        prtos_s32_t used_msgs;
        partition_t *receiver;
        prtos_s32_t receiver_port;
        partition_t *sender;
        prtos_s32_t sender_port;
        spin_lock_t lock;
    } q;
};

struct port {
    prtos_u32_t flags;
#define COMM_PORT_OPENED 0x1
#define COMM_PORT_EMPTY 0x0
#define COMM_PORT_NEW_MSG 0x2
#define COMM_PORT_CONSUMED_MSG 0x4
#define COMM_PORT_MSG_MASK 0x6
    prtos_id_t partition_id;
    spin_lock_t lock;
};

#endif

#endif
