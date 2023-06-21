/*
 * FILE: comm.h
 *
 * Comm ports
 *
 * www.prtos.org
 */

#ifndef _LIB_PRTOS_COMM_H_
#define _LIB_PRTOS_COMM_H_

#include <prtos_inc/config.h>
#include <prtos_inc/objdir.h>
#include <prtos_inc/objects/commports.h>

// Sampling port related functions
extern prtos_s32_t prtos_create_sampling_port(char *port_name, prtos_u32_t max_msg_size, prtos_u32_t direction, prtos_time_t valid_period);
extern prtos_s32_t prtos_read_sampling_message(prtos_s32_t port_id, void *msg_ptr, prtos_u32_t msg_size, prtos_u32_t *flags);
extern prtos_s32_t prtos_write_sampling_message(prtos_s32_t port_id, void *msg_ptr, prtos_u32_t msg_size);
extern prtos_s32_t prtos_get_sampling_port_status(prtos_u32_t port_id, prtos_sampling_port_status_t *status);
extern prtos_s32_t prtos_get_sampling_port_info(char *port_name, prtos_sampling_port_info_t *info);

// Queuing port related functions
extern prtos_s32_t prtos_create_queuing_port(char *port_name, prtos_u32_t max_num_of_msgs, prtos_u32_t max_msg_size, prtos_u32_t direction);
extern prtos_s32_t prtos_send_queuing_message(prtos_s32_t port_id, void *msg_ptr, prtos_u32_t msg_size);
extern prtos_s32_t prtos_receive_queuing_message(prtos_s32_t port_id, void *msg_ptr, prtos_u32_t msg_size);
extern prtos_s32_t prtos_get_queuing_port_status(prtos_u32_t port_id, prtos_queuing_port_status_t *status);
extern prtos_s32_t prtos_get_queuing_port_info(char *port_name, prtos_queuing_port_info_t *info);

#endif
