/*
 * FILE: comm.c
 *
 * Communication wrappers
 *
 * www.prtos.org
 */

#include <prtos.h>
#include <comm.h>
#include <hypervisor.h>
#include <prtoshypercalls.h>

#include <prtos_inc/hypercalls.h>

prtos_s32_t prtos_create_sampling_port(char *port_name, prtos_u32_t max_msg_size, prtos_u32_t direction, prtos_time_t valid_period) {
    prtos_obj_desc_t desc = OBJDESC_BUILD(OBJ_CLASS_SAMPLING_PORT, PRTOS_PARTITION_SELF, 0);
    union sampling_port_cmd cmd;
    prtos_s32_t id;

    cmd.create.port_name = port_name;
    cmd.create.max_msg_size = max_msg_size;
    cmd.create.direction = direction;
    cmd.create.valid_period = valid_period;

    id = prtos_ctrl_object(desc, PRTOS_COMM_CREATE_PORT, &cmd);
    return id;
}

prtos_s32_t prtos_read_sampling_message(prtos_s32_t port_id, void *msg_ptr, prtos_u32_t msg_size, prtos_u32_t *flags) {
    prtos_obj_desc_t desc = OBJDESC_BUILD(OBJ_CLASS_SAMPLING_PORT, PRTOS_PARTITION_SELF, port_id);
    return prtos_read_object(desc, msg_ptr, msg_size, flags);
}

prtos_s32_t prtos_write_sampling_message(prtos_s32_t port_id, void *msg_ptr, prtos_u32_t msg_size) {
    prtos_obj_desc_t desc = OBJDESC_BUILD(OBJ_CLASS_SAMPLING_PORT, PRTOS_PARTITION_SELF, port_id);
    return prtos_write_object(desc, msg_ptr, msg_size, 0);
}

prtos_s32_t prtos_create_queuing_port(char *port_name, prtos_u32_t max_num_of_msgs, prtos_u32_t max_msg_size, prtos_u32_t direction) {
    prtos_obj_desc_t desc = OBJDESC_BUILD(OBJ_CLASS_QUEUING_PORT, PRTOS_PARTITION_SELF, 0);
    union queuing_port_cmd cmd;
    prtos_s32_t id;

    cmd.create.port_name = port_name;
    cmd.create.max_num_of_msgs = max_num_of_msgs;
    cmd.create.max_msg_size = max_msg_size;
    cmd.create.direction = direction;

    id = prtos_ctrl_object(desc, PRTOS_COMM_CREATE_PORT, &cmd);
    return id;
}

prtos_s32_t prtos_send_queuing_message(prtos_s32_t port_id, void *msg_ptr, prtos_u32_t msg_size) {
    prtos_obj_desc_t desc = OBJDESC_BUILD(OBJ_CLASS_QUEUING_PORT, PRTOS_PARTITION_SELF, port_id);

    return prtos_write_object(desc, msg_ptr, msg_size, 0);
}

prtos_s32_t prtos_receive_queuing_message(prtos_s32_t port_id, void *msg_ptr, prtos_u32_t msg_size) {
    prtos_obj_desc_t desc = OBJDESC_BUILD(OBJ_CLASS_QUEUING_PORT, PRTOS_PARTITION_SELF, port_id);
    return prtos_read_object(desc, msg_ptr, msg_size, 0);
}

prtos_s32_t prtos_get_queuing_port_status(prtos_u32_t port_id, prtos_queuing_port_status_t *status) {
    prtos_obj_desc_t desc = OBJDESC_BUILD(OBJ_CLASS_QUEUING_PORT, PRTOS_PARTITION_SELF, port_id);
    if (!status) return PRTOS_PARTITION_SELF;

    if (prtos_ctrl_object(desc, PRTOS_COMM_GET_PORT_STATUS, status) != PRTOS_OK) return PRTOS_PARTITION_SELF;

    return PRTOS_OK;
}

prtos_s32_t prtos_get_queuing_port_info(char *port_name, prtos_queuing_port_info_t *info) {
    prtos_obj_desc_t desc = OBJDESC_BUILD(OBJ_CLASS_QUEUING_PORT, PRTOS_PARTITION_SELF, 0);
    if (!info) return PRTOS_PARTITION_SELF;

    info->port_name = port_name;
    if (prtos_ctrl_object(desc, PRTOS_COMM_GET_PORT_INFO, (union queuing_port_cmd *)info) != PRTOS_OK) return PRTOS_PARTITION_SELF;
    return PRTOS_OK;
}

prtos_s32_t prtos_get_sampling_port_status(prtos_u32_t port_id, prtos_sampling_port_status_t *status) {
    prtos_obj_desc_t desc = OBJDESC_BUILD(OBJ_CLASS_QUEUING_PORT, PRTOS_PARTITION_SELF, 0);
    if (!status) return PRTOS_PARTITION_SELF;

    if (prtos_ctrl_object(desc, PRTOS_COMM_GET_PORT_STATUS, status) != PRTOS_OK) return PRTOS_PARTITION_SELF;
    return PRTOS_OK;
}

prtos_s32_t prtos_get_sampling_port_info(char *port_name, prtos_sampling_port_info_t *info) {
    prtos_obj_desc_t desc = OBJDESC_BUILD(OBJ_CLASS_SAMPLING_PORT, PRTOS_PARTITION_SELF, 0);
    if (!info) return PRTOS_PARTITION_SELF;

    info->port_name = port_name;
    if (prtos_ctrl_object(desc, PRTOS_COMM_GET_PORT_INFO, (union sampling_port_cmd *)info) != PRTOS_OK) return PRTOS_PARTITION_SELF;
    return PRTOS_OK;
}
