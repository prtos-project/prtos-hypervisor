/*
 * FILE: commports.c
 *
 * Inter-partition communication mechanisms
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <list.h>
#include <gaccess.h>
#include <kthread.h>
#include <hypercalls.h>
#include <rsvmem.h>
#include <sched.h>
#include <spinlock.h>
#include <stdc.h>
#include <prtosconf.h>
#include <objects/commports.h>
#ifdef CONFIG_OBJ_STATUS_ACC
#include <objects/status.h>
#endif

static union channel *channel_table;
static struct port *port_table;

static inline prtos_s32_t create_sampling_port(prtos_obj_desc_t desc, prtos_s8_t *__g_param port_name, prtos_u32_t max_msg_size, prtos_u32_t direction,
                                               prtos_time_t valid_period) {
    local_processor_t *processor_info = GET_LOCAL_PROCESSOR();
    struct prtos_conf_part *partition;
    prtos_u32_t flags;
    prtos_s32_t port;

    partition = get_partition(processor_info->sched.current_kthread)->cfg;

    if (OBJDESC_GET_PARTITIONID(desc) != partition->id) return PRTOS_PERM_ERROR;

    if (check_gp_aram(port_name, CONFIG_ID_STRING_LENGTH, 1, PFLAG_NOT_NULL | PFLAG_RW) < 0) {
        return PRTOS_INVALID_PARAM;
    }

    if ((direction != PRTOS_SOURCE_PORT) && (direction != PRTOS_DESTINATION_PORT)) {
        return PRTOS_INVALID_PARAM;
    }
    // Look for the channel for sampling port created
    for (port = partition->comm_ports_offset; port < (partition->num_of_ports + partition->comm_ports_offset); port++)
        if (!strncmp(port_name, &prtos_conf_string_tab[prtos_conf_comm_ports[port].name_offset], CONFIG_ID_STRING_LENGTH)) break;

    if (port >= prtos_conf_table.num_of_comm_ports) {
        return PRTOS_INVALID_PARAM;
    }
    if (prtos_conf_comm_ports[port].type != PRTOS_SAMPLING_PORT) return PRTOS_INVALID_CONFIG;

    if (direction != prtos_conf_comm_ports[port].direction) return PRTOS_INVALID_CONFIG;

    spin_lock(&port_table[port].lock);
    flags = port_table[port].flags;
    spin_unlock(&port_table[port].lock);

    if (flags & COMM_PORT_OPENED) return PRTOS_NO_ACTION;

    if (prtos_conf_comm_ports[port].channel_id != PRTOS_NULL_CHANNEL) {
        ASSERT((prtos_conf_comm_ports[port].channel_id >= 0) && (prtos_conf_comm_ports[port].channel_id < prtos_conf_table.num_of_comm_channels));
        if (prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id].s.max_length != max_msg_size) return PRTOS_INVALID_CONFIG;

        if (prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id].s.valid_period != valid_period) return PRTOS_INVALID_CONFIG;

        spin_lock(&channel_table[prtos_conf_comm_ports[port].channel_id].s.lock);
        if (direction == PRTOS_DESTINATION_PORT) {
            ASSERT_LOCK(channel_table[prtos_conf_comm_ports[port].channel_id].s.num_of_receivers <
                            prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id].s.num_of_receivers,
                        &channel_table[prtos_conf_comm_ports[port].channel_id].s.lock);
            channel_table[prtos_conf_comm_ports[port].channel_id].s.receiver_table[channel_table[prtos_conf_comm_ports[port].channel_id].s.num_of_receivers] =
                get_partition(processor_info->sched.current_kthread);
            channel_table[prtos_conf_comm_ports[port].channel_id]
                .s.receiver_port_table[channel_table[prtos_conf_comm_ports[port].channel_id].s.num_of_receivers] = port - partition->comm_ports_offset;
            channel_table[prtos_conf_comm_ports[port].channel_id].s.num_of_receivers++;
        } else {  // PRTOS_SOURCE_PORT
            channel_table[prtos_conf_comm_ports[port].channel_id].s.sender = get_partition(processor_info->sched.current_kthread);
            channel_table[prtos_conf_comm_ports[port].channel_id].s.sender_port = port - partition->comm_ports_offset;
        }
        spin_unlock(&channel_table[prtos_conf_comm_ports[port].channel_id].s.lock);
    }

    spin_lock(&port_table[port].lock);
    port_table[port].flags |= COMM_PORT_OPENED | COMM_PORT_EMPTY;
    port_table[port].partition_id = KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id);
    spin_unlock(&port_table[port].lock);
    return port;
}

static prtos_s32_t read_sampling_port(prtos_obj_desc_t desc, void *__g_param msg_ptr, prtos_u_size_t msg_size, prtos_u32_t *__g_param flags) {
    local_processor_t *processor_info = GET_LOCAL_PROCESSOR();
    prtos_s32_t port = OBJDESC_GET_ID(desc);
    struct prtos_conf_comm_channel *prtos_conf_comm_channel;
    union channel *channel;
    prtos_u_size_t ret_size = 0;
    struct guest *g;
    prtos_word_t *comm_port_bitmap;
    prtos_id_t partition_id;
    prtos_u32_t port_flags;

    if (OBJDESC_GET_PARTITIONID(desc) != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_PERM_ERROR;

    if (check_gp_aram(flags, sizeof(prtos_u32_t), 4, PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;

    spin_lock(&port_table[port].lock);
    partition_id = port_table[port].partition_id;
    port_flags = port_table[port].flags;
    spin_unlock(&port_table[port].lock);
    // reading a port which does not belong to this partition
    if (partition_id != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_INVALID_PARAM;

    if (!(port_flags & COMM_PORT_OPENED)) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].type != PRTOS_SAMPLING_PORT) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].direction != PRTOS_DESTINATION_PORT) return PRTOS_INVALID_PARAM;

    if (!msg_size) return PRTOS_INVALID_CONFIG;

    if (prtos_conf_comm_ports[port].channel_id != PRTOS_NULL_CHANNEL) {
        prtos_conf_comm_channel = &prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id];
        if (msg_size > prtos_conf_comm_channel->s.max_length) return PRTOS_INVALID_CONFIG;

        if (check_gp_aram(msg_ptr, msg_size, 1, PFLAG_NOT_NULL | PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;

        channel = &channel_table[prtos_conf_comm_ports[port].channel_id];
        spin_lock(&channel->s.lock);
        ret_size = (msg_size < channel->s.length) ? msg_size : channel->s.length;
        memcpy(msg_ptr, channel->s.buffer, ret_size);
        spin_lock(&port_table[port].lock);
        port_table[port].flags &= ~COMM_PORT_MSG_MASK;
        port_table[port].flags |= COMM_PORT_CONSUMED_MSG;
        spin_unlock(&port_table[port].lock);
#ifdef CONFIG_OBJ_STATUS_ACC
        system_status.num_of_sampling_port_msgs_read++;
        if (processor_info->sched.current_kthread->ctrl.g)
            partition_status[KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)].num_of_sampling_port_msgs_read++;
#endif

        g = processor_info->sched.current_kthread->ctrl.g;
        comm_port_bitmap =
            (prtos_word_t *)((prtos_address_t)g->part_ctrl_table + sizeof(partition_control_table_t) +
                             sizeof(struct prtos_physical_mem_map) * get_partition(processor_info->sched.current_kthread)->cfg->num_of_physical_memory_areas);
        ASSERT((port - get_partition(processor_info->sched.current_kthread)->cfg->comm_ports_offset) < g->part_ctrl_table->num_of_comm_ports);
        prtos_clear_bit(comm_port_bitmap, (port - get_partition(processor_info->sched.current_kthread)->cfg->comm_ports_offset),
                        g->part_ctrl_table->num_of_comm_ports);

        if (channel->s.sender) {
            prtos_s32_t e;
            for (e = 0; e < channel->s.sender->cfg->num_of_vcpus; e++) {
                g = channel->s.sender->kthread[e]->ctrl.g;
                comm_port_bitmap = (prtos_word_t *)((prtos_address_t)g->part_ctrl_table + sizeof(partition_control_table_t) +
                                                    sizeof(struct prtos_physical_mem_map) *
                                                        get_partition(processor_info->sched.current_kthread)->cfg->num_of_physical_memory_areas);
                prtos_clear_bit(comm_port_bitmap, channel->s.sender_port, g->part_ctrl_table->num_of_comm_ports);
            }
            set_part_ext_irq_pending(channel->s.sender, PRTOS_VT_EXT_SAMPLING_PORT);
        }

        if (flags) {
            *flags = 0;
            if (ret_size && (prtos_conf_comm_channel->s.valid_period != PRTOS_INFINITE_TIME) &&
                (channel->s.timestamp + prtos_conf_comm_channel->s.valid_period) > get_sys_clock_usec())
                *flags = PRTOS_COMM_MSG_VALID;
        }
        spin_unlock(&channel->s.lock);
    }

    return ret_size;
}

static prtos_s32_t write_sampling_port(prtos_obj_desc_t desc, void *__g_param msg_ptr, prtos_u_size_t msg_size) {
    local_processor_t *processor_info = GET_LOCAL_PROCESSOR();
    prtos_s32_t port = OBJDESC_GET_ID(desc);
    struct prtos_conf_comm_channel *prtos_conf_comm_channel;
    union channel *channel;
    prtos_s32_t e, i;
    struct guest *g;
    prtos_word_t *comm_port_bitmap;

    if (OBJDESC_GET_PARTITIONID(desc) != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_PERM_ERROR;

    // reading a port which does not belong to this partition
    if (port_table[port].partition_id != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_INVALID_PARAM;

    if (!(port_table[port].flags & COMM_PORT_OPENED)) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].type != PRTOS_SAMPLING_PORT) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].direction != PRTOS_SOURCE_PORT) return PRTOS_INVALID_PARAM;

    if (!msg_size) return PRTOS_INVALID_CONFIG;

    if (prtos_conf_comm_ports[port].channel_id != PRTOS_NULL_CHANNEL) {
        prtos_conf_comm_channel = &prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id];
        if (msg_size > prtos_conf_comm_channel->s.max_length) return PRTOS_INVALID_CONFIG;

        if (check_gp_aram(msg_ptr, msg_size, 1, PFLAG_NOT_NULL | PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;

        channel = &channel_table[prtos_conf_comm_ports[port].channel_id];
        spin_lock(&channel->s.lock);
        memcpy(channel->s.buffer, msg_ptr, msg_size);
#ifdef CONFIG_OBJ_STATUS_ACC
        system_status.num_of_sampling_port_msgs_written++;
        if (processor_info->sched.current_kthread->ctrl.g)
            partition_status[KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)].num_of_sampling_port_msgs_written++;
#endif
        ASSERT_LOCK(channel->s.sender == get_partition(processor_info->sched.current_kthread), &channel->s.lock);
        for (e = 0; e < get_partition(processor_info->sched.current_kthread)->cfg->num_of_vcpus; e++) {
            g = get_partition(processor_info->sched.current_kthread)->kthread[e]->ctrl.g;
            comm_port_bitmap = (prtos_word_t *)((prtos_address_t)g->part_ctrl_table + sizeof(partition_control_table_t) +
                                                sizeof(struct prtos_physical_mem_map) *
                                                    get_partition(processor_info->sched.current_kthread)->cfg->num_of_physical_memory_areas);
            prtos_set_bit(comm_port_bitmap, port, g->part_ctrl_table->num_of_comm_ports);
        }

        for (e = 0; e < channel->s.num_of_receivers; e++) {
            if (channel->s.receiver_table[e]) {
                for (i = 0; i < channel->s.receiver_table[e]->cfg->num_of_vcpus; i++) {
                    g = channel->s.receiver_table[e]->kthread[i]->ctrl.g;
                    ASSERT_LOCK(channel->s.receiver_port_table[e] < g->part_ctrl_table->num_of_comm_ports, &channel->s.lock);
                    comm_port_bitmap = (prtos_word_t *)((prtos_address_t)g->part_ctrl_table + sizeof(partition_control_table_t) +
                                                        sizeof(struct prtos_physical_mem_map) *
                                                            get_partition(processor_info->sched.current_kthread)->cfg->num_of_physical_memory_areas);
                    prtos_set_bit(comm_port_bitmap, channel->s.receiver_port_table[e], g->part_ctrl_table->num_of_comm_ports);
                    port_table[channel->s.receiver_port_table[e]].flags &= ~COMM_PORT_MSG_MASK;
                    port_table[channel->s.receiver_port_table[e]].flags |= COMM_PORT_NEW_MSG;
                }
                set_part_ext_irq_pending(channel->s.receiver_table[e], PRTOS_VT_EXT_SAMPLING_PORT);
            }
        }
        channel->s.length = msg_size;
        channel->s.timestamp = get_sys_clock_usec();
        spin_unlock(&channel->s.lock);
    }

    return PRTOS_OK;
}

static inline prtos_s32_t get_samping_port_info(prtos_obj_desc_t desc, prtos_sampling_port_info_t *info) {
    local_processor_t *processor_info = GET_LOCAL_PROCESSOR();
    struct prtos_conf_part *partition;
    struct prtos_conf_comm_channel *prtos_conf_comm_channel;
    prtos_s32_t port;

    partition = get_partition(processor_info->sched.current_kthread)->cfg;

    if (OBJDESC_GET_PARTITIONID(desc) != partition->id) return PRTOS_PERM_ERROR;

    if (check_gp_aram(info->port_name, CONFIG_ID_STRING_LENGTH, 1, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    // Look for the channel
    for (port = partition->comm_ports_offset; port < (partition->num_of_ports + partition->comm_ports_offset); port++)
        if (!strcmp(info->port_name, &prtos_conf_string_tab[prtos_conf_comm_ports[port].name_offset])) break;

    if (port >= prtos_conf_table.num_of_comm_ports) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].type != PRTOS_SAMPLING_PORT) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].channel_id != PRTOS_NULL_CHANNEL) {
        prtos_conf_comm_channel = &prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id];
        info->valid_period = prtos_conf_comm_channel->s.valid_period;
        info->max_msg_size = prtos_conf_comm_channel->s.max_length;
        info->direction = prtos_conf_comm_ports[port].direction;
    } else {
        info->valid_period = PRTOS_INFINITE_TIME;
        info->max_msg_size = 0;
        info->direction = 0;
    }

    return PRTOS_OK;
}

static inline prtos_s32_t get_sampling_port_status(prtos_obj_desc_t desc, prtos_sampling_port_status_t *status) {
    local_processor_t *processor_info = GET_LOCAL_PROCESSOR();
    prtos_s32_t port = OBJDESC_GET_ID(desc);
    struct prtos_conf_comm_channel *prtos_conf_comm_channel;
    union channel *channel;
    prtos_u32_t flags;
    prtos_id_t partition_id;

    if (OBJDESC_GET_PARTITIONID(desc) != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_PERM_ERROR;

    spin_lock(&port_table[port].lock);
    partition_id = port_table[port].partition_id;
    flags = port_table[port].flags;
    spin_unlock(&port_table[port].lock);

    // reading a port which does not belong to this partition
    if (partition_id != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_INVALID_PARAM;

    if (!(flags & COMM_PORT_OPENED)) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].type != PRTOS_SAMPLING_PORT) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].channel_id != PRTOS_NULL_CHANNEL) {
        prtos_conf_comm_channel = &prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id];
        status->flags = 0;
        channel = &channel_table[prtos_conf_comm_ports[port].channel_id];
        spin_lock(&channel->s.lock);
        status->last_msg_size = channel->s.length;
        status->timestamp = channel->s.timestamp;
        if (flags & COMM_PORT_NEW_MSG)
            status->flags |= PRTOS_COMM_NEW_MSG;
        else if (flags & COMM_PORT_CONSUMED_MSG) {
            status->flags |= PRTOS_COMM_CONSUMED_MSG;
        } else {
            status->flags |= PRTOS_COMM_EMPTY_PORT;
        }

        if (channel->s.timestamp && (prtos_conf_comm_channel->s.valid_period != PRTOS_INFINITE_TIME) &&
            ((channel->s.timestamp + prtos_conf_comm_channel->s.valid_period) > get_sys_clock_usec()))
            status->flags |= PRTOS_COMM_MSG_VALID;
        spin_unlock(&channel->s.lock);
    } else
        memset(status, 0, sizeof(prtos_sampling_port_status_t));
    return PRTOS_OK;
}

static prtos_s32_t ctrl_sampling_port(prtos_obj_desc_t desc, prtos_u32_t cmd, union sampling_port_cmd *__g_param args) {
    if (check_gp_aram(args, sizeof(union sampling_port_cmd), 4, PFLAG_NOT_NULL | PFLAG_RW) < 0) {
        return PRTOS_INVALID_PARAM;
    }
    switch (cmd) {
        case PRTOS_COMM_CREATE_PORT:
            if (!args->create.port_name || (check_gp_aram(args->create.port_name, CONFIG_ID_STRING_LENGTH, 1, PFLAG_NOT_NULL) < 0)) {
                return PRTOS_INVALID_PARAM;
            }
            return create_sampling_port(desc, args->create.port_name, args->create.max_msg_size, args->create.direction, args->create.valid_period);
        case PRTOS_COMM_GET_PORT_STATUS:
            return get_sampling_port_status(desc, &args->status);
        case PRTOS_COMM_GET_PORT_INFO:
            return get_samping_port_info(desc, &args->info);
    }
    return PRTOS_INVALID_PARAM;
}

static const struct object sampling_port_obj = {
    .read = (read_obj_op_t)read_sampling_port,
    .write = (write_obj_op_t)write_sampling_port,
    .ctrl = (ctrl_obj_op_t)ctrl_sampling_port,
};

void reset_part_ports(partition_t *p) {
    struct prtos_conf_part *partition;
    prtos_s32_t port;

    partition = p->cfg;

    for (port = partition->comm_ports_offset; port < (partition->num_of_ports + partition->comm_ports_offset); port++) {
        port_table[port].flags &= ~COMM_PORT_OPENED;
    }
}

static inline prtos_s32_t create_queuing_port(prtos_obj_desc_t desc, prtos_s8_t *__g_param port_name, prtos_s32_t max_num_of_msgs, prtos_s32_t max_msg_size,
                                              prtos_u32_t direction) {
    local_processor_t *processor_info = GET_LOCAL_PROCESSOR();
    struct prtos_conf_part *partition;
    prtos_s32_t port;
    prtos_u32_t flags;

    partition = get_partition(processor_info->sched.current_kthread)->cfg;
    if (OBJDESC_GET_PARTITIONID(desc) != partition->id) return PRTOS_PERM_ERROR;

    if (check_gp_aram(port_name, CONFIG_ID_STRING_LENGTH, 1, PFLAG_NOT_NULL | PFLAG_RW) < 0) {
        return PRTOS_INVALID_PARAM;
    }
    if ((direction != PRTOS_SOURCE_PORT) && (direction != PRTOS_DESTINATION_PORT)) return PRTOS_INVALID_PARAM;

    // Look for the channel
    for (port = partition->comm_ports_offset; port < (partition->num_of_ports + partition->comm_ports_offset); port++)
        if (!strncmp(port_name, &prtos_conf_string_tab[prtos_conf_comm_ports[port].name_offset], CONFIG_ID_STRING_LENGTH)) break;

    if (port >= prtos_conf_table.num_of_comm_ports) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].type != PRTOS_QUEUING_PORT) return PRTOS_INVALID_CONFIG;

    if (direction != prtos_conf_comm_ports[port].direction) return PRTOS_INVALID_CONFIG;

    spin_lock(&port_table[port].lock);
    flags = port_table[port].flags;
    spin_unlock(&port_table[port].lock);

    if (flags & COMM_PORT_OPENED) return PRTOS_NO_ACTION;

    if (prtos_conf_comm_ports[port].channel_id != PRTOS_NULL_CHANNEL) {
        ASSERT((prtos_conf_comm_ports[port].channel_id >= 0) && (prtos_conf_comm_ports[port].channel_id < prtos_conf_table.num_of_comm_channels));
        spin_lock(&channel_table[prtos_conf_comm_ports[port].channel_id].q.lock);
        if (direction == PRTOS_DESTINATION_PORT) {
            channel_table[prtos_conf_comm_ports[port].channel_id].q.receiver = get_partition(processor_info->sched.current_kthread);
            channel_table[prtos_conf_comm_ports[port].channel_id].q.receiver_port = port - partition->comm_ports_offset;
        } else {  // PRTOS_SOURCE_PORT
            channel_table[prtos_conf_comm_ports[port].channel_id].q.sender = get_partition(processor_info->sched.current_kthread);
            channel_table[prtos_conf_comm_ports[port].channel_id].q.sender_port = port - partition->comm_ports_offset;
        }
        spin_unlock(&channel_table[prtos_conf_comm_ports[port].channel_id].q.lock);
        if (prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id].q.max_num_of_msgs != max_num_of_msgs) return PRTOS_INVALID_CONFIG;
        if (prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id].q.max_length != max_msg_size) return PRTOS_INVALID_CONFIG;
    }

    spin_lock(&port_table[port].lock);
    port_table[port].flags |= COMM_PORT_OPENED;
    port_table[port].partition_id = KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id);
    spin_unlock(&port_table[port].lock);
    return port;
}

static prtos_s32_t send_queuing_port(prtos_obj_desc_t desc, void *__g_param msg_ptr, prtos_u32_t msg_size) {
    local_processor_t *processor_info = GET_LOCAL_PROCESSOR();
    prtos_s32_t port = OBJDESC_GET_ID(desc), e;
    struct prtos_conf_comm_channel *prtos_conf_comm_channel;
    union channel *channel;
    struct msg *msg;
    struct guest *g;
    prtos_word_t *comm_port_bitmap;
    prtos_u32_t flags;
    prtos_id_t partition_id;

    if (OBJDESC_GET_PARTITIONID(desc) != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_PERM_ERROR;

    spin_lock(&port_table[port].lock);
    partition_id = port_table[port].partition_id;
    flags = port_table[port].flags;
    spin_unlock(&port_table[port].lock);

    // reading a port which does not belong to this partition
    if (partition_id != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_INVALID_PARAM;
    if (!(flags & COMM_PORT_OPENED)) return PRTOS_INVALID_PARAM;
    if (prtos_conf_comm_ports[port].type != PRTOS_QUEUING_PORT) return PRTOS_INVALID_PARAM;
    if (prtos_conf_comm_ports[port].direction != PRTOS_SOURCE_PORT) return PRTOS_INVALID_PARAM;
    if (!msg_size) return PRTOS_INVALID_CONFIG;

    if (prtos_conf_comm_ports[port].channel_id != PRTOS_NULL_CHANNEL) {
        prtos_conf_comm_channel = &prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id];
        if (msg_size > prtos_conf_comm_channel->q.max_length) return PRTOS_INVALID_CONFIG;

        if (check_gp_aram(msg_ptr, msg_size, 1, PFLAG_NOT_NULL | PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;

        channel = &channel_table[prtos_conf_comm_ports[port].channel_id];
        spin_lock(&channel->q.lock);
        if (channel->q.used_msgs < prtos_conf_comm_channel->q.max_num_of_msgs) {
            if (!(msg = (struct msg *)dyn_list_remove_tail(&channel->q.free_msgs))) {
                cpu_ctxt_t ctxt;
                spin_unlock(&channel->q.lock);
                get_cpu_ctxt(&ctxt);
                system_panic(&ctxt, "[send_queuing_port] Queuing channels internal error");
            }
            memcpy(msg->buffer, msg_ptr, msg_size);
#ifdef CONFIG_OBJ_STATUS_ACC
            system_status.num_of_queuing_port_msgs_sent++;
            if (processor_info->sched.current_kthread->ctrl.g)
                partition_status[KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)].num_of_queuing_port_msgs_sent++;
#endif
            msg->length = msg_size;
            dyn_list_insert_head(&channel->q.recv_msgs, &msg->list_node);
            channel->q.used_msgs++;

            if (channel->q.receiver) {
                for (e = 0; e < channel->q.receiver->cfg->num_of_vcpus; e++) {
                    g = channel->q.receiver->kthread[e]->ctrl.g;
                    comm_port_bitmap = (prtos_word_t *)((prtos_address_t)g->part_ctrl_table + sizeof(partition_control_table_t) +
                                                        sizeof(struct prtos_physical_mem_map) *
                                                            get_partition(processor_info->sched.current_kthread)->cfg->num_of_physical_memory_areas);

                    ASSERT_LOCK(channel->q.receiver_port < g->part_ctrl_table->num_of_comm_ports, &channel->q.lock);
                    prtos_set_bit(comm_port_bitmap, channel->q.receiver_port, g->part_ctrl_table->num_of_comm_ports);
                }
                set_part_ext_irq_pending(channel->q.receiver, PRTOS_VT_EXT_QUEUING_PORT);
            }

            for (e = 0; e < get_partition(processor_info->sched.current_kthread)->cfg->num_of_vcpus; e++) {
                g = get_partition(processor_info->sched.current_kthread)->kthread[e]->ctrl.g;
                comm_port_bitmap = (prtos_word_t *)((prtos_address_t)g->part_ctrl_table + sizeof(partition_control_table_t) +
                                                    sizeof(struct prtos_physical_mem_map) *
                                                        get_partition(processor_info->sched.current_kthread)->cfg->num_of_physical_memory_areas);
                ASSERT_LOCK(channel->q.sender == get_partition(processor_info->sched.current_kthread), &channel->q.lock);
                prtos_set_bit(comm_port_bitmap, channel->q.sender_port, g->part_ctrl_table->num_of_comm_ports);
            }
        } else {
            spin_unlock(&channel->q.lock);
            return PRTOS_OP_NOT_ALLOWED;
        }
        spin_unlock(&channel->q.lock);
    }

    return PRTOS_OK;
}

static prtos_s32_t receive_queuing_port(prtos_obj_desc_t desc, void *__g_param msg_ptr, prtos_u32_t msg_size) {
    local_processor_t *processor_info = GET_LOCAL_PROCESSOR();
    prtos_s32_t port = OBJDESC_GET_ID(desc);
    struct prtos_conf_comm_channel *prtos_conf_comm_channel;
    union channel *channel;
    prtos_u_size_t ret_size = 0;
    struct msg *msg;
    prtos_id_t partition_id;
    prtos_u32_t flags;

    if (OBJDESC_GET_PARTITIONID(desc) != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_PERM_ERROR;

    spin_lock(&port_table[port].lock);
    partition_id = port_table[port].partition_id;
    flags = port_table[port].flags;
    spin_unlock(&port_table[port].lock);

    // reading a port which does not belong to this partition
    if (partition_id != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_INVALID_PARAM;

    if (!(flags & COMM_PORT_OPENED)) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].type != PRTOS_QUEUING_PORT) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].direction != PRTOS_DESTINATION_PORT) return PRTOS_INVALID_PARAM;

    if (!msg_size) return PRTOS_INVALID_CONFIG;

    if (prtos_conf_comm_ports[port].channel_id != PRTOS_NULL_CHANNEL) {
        prtos_conf_comm_channel = &prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id];
        if (msg_size > prtos_conf_comm_channel->q.max_length) return PRTOS_INVALID_CONFIG;

        if (check_gp_aram(msg_ptr, msg_size, 1, PFLAG_NOT_NULL | PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;

        channel = &channel_table[prtos_conf_comm_ports[port].channel_id];
        spin_lock(&channel->q.lock);
        if (channel->q.used_msgs > 0) {
            if (!(msg = (struct msg *)dyn_list_remove_tail(&channel->q.recv_msgs))) {
                cpu_ctxt_t ctxt;
                spin_unlock(&channel->q.lock);
                get_cpu_ctxt(&ctxt);
                system_panic(&ctxt, "[receive_queuing_port] Queuing channels internal error");
            }
            ret_size = (msg_size < msg->length) ? msg_size : msg->length;
            memcpy(msg_ptr, msg->buffer, ret_size);
#ifdef CONFIG_OBJ_STATUS_ACC
            system_status.num_of_queuing_port_msgs_received++;
            if (processor_info->sched.current_kthread->ctrl.g)
                partition_status[KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)].num_of_queuing_port_msgs_received++;
#endif
            dyn_list_insert_head(&channel->q.free_msgs, &msg->list_node);
            channel->q.used_msgs--;

            if (channel->q.sender) set_part_ext_irq_pending(channel->q.sender, PRTOS_VT_EXT_QUEUING_PORT);

            if (!channel->q.used_msgs) {
                prtos_s32_t e;
                struct guest *g;
                prtos_word_t *comm_port_bitmap;
                for (e = 0; e < get_partition(processor_info->sched.current_kthread)->cfg->num_of_vcpus; e++) {
                    g = get_partition(processor_info->sched.current_kthread)->kthread[e]->ctrl.g;
                    comm_port_bitmap = (prtos_word_t *)((prtos_address_t)g->part_ctrl_table + sizeof(partition_control_table_t) +
                                                        sizeof(struct prtos_physical_mem_map) *
                                                            get_partition(processor_info->sched.current_kthread)->cfg->num_of_physical_memory_areas);

                    ASSERT_LOCK((port - get_partition(processor_info->sched.current_kthread)->cfg->comm_ports_offset) < g->part_ctrl_table->num_of_comm_ports,
                                &channel->q.lock);
                    prtos_clear_bit(comm_port_bitmap, (get_partition(processor_info->sched.current_kthread)->cfg->comm_ports_offset),
                                    g->part_ctrl_table->num_of_comm_ports);
                }

                if (channel->q.sender) {
                    for (e = 0; e < channel->q.sender->cfg->num_of_vcpus; e++) {
                        g = channel->q.sender->kthread[e]->ctrl.g;
                        comm_port_bitmap = (prtos_word_t *)((prtos_address_t)g->part_ctrl_table + sizeof(partition_control_table_t) +
                                                            sizeof(struct prtos_physical_mem_map) *
                                                                get_partition(processor_info->sched.current_kthread)->cfg->num_of_physical_memory_areas);

                        ASSERT_LOCK(channel->q.sender_port < g->part_ctrl_table->num_of_comm_ports, &channel->q.lock);
                        prtos_clear_bit(comm_port_bitmap, channel->q.sender_port, g->part_ctrl_table->num_of_comm_ports);
                    }
                }
            }
        } else {
            spin_unlock(&channel->q.lock);
            return PRTOS_OP_NOT_ALLOWED;
        }
        spin_unlock(&channel->q.lock);
    }
    return ret_size;
}

static inline prtos_s32_t get_queuling_port_status(prtos_obj_desc_t desc, prtos_queuing_port_status_t *status) {
    local_processor_t *processor_info = GET_LOCAL_PROCESSOR();
    prtos_s32_t port = OBJDESC_GET_ID(desc);
    prtos_id_t partition_id;
    prtos_u32_t flags;

    if (OBJDESC_GET_PARTITIONID(desc) != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_PERM_ERROR;

    spin_lock(&port_table[port].lock);
    partition_id = port_table[port].partition_id;
    flags = port_table[port].flags;
    spin_unlock(&port_table[port].lock);

    // reading a port which does not belong to this partition
    if (partition_id != KID2PARTID(processor_info->sched.current_kthread->ctrl.g->id)) return PRTOS_INVALID_PARAM;

    if (!(flags & COMM_PORT_OPENED)) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].type != PRTOS_QUEUING_PORT) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].channel_id != PRTOS_NULL_CHANNEL) {
        spin_lock(&channel_table[prtos_conf_comm_ports[port].channel_id].q.lock);
        status->num_of_msgs = channel_table[prtos_conf_comm_ports[port].channel_id].q.used_msgs;
        spin_unlock(&channel_table[prtos_conf_comm_ports[port].channel_id].q.lock);
    } else
        memset(status, 0, sizeof(prtos_sampling_port_status_t));

    return PRTOS_OK;
}

static inline prtos_s32_t get_queuling_port_info(prtos_obj_desc_t desc, prtos_queuing_port_info_t *info) {
    local_processor_t *processor_info = GET_LOCAL_PROCESSOR();
    struct prtos_conf_part *partition;
    struct prtos_conf_comm_channel *prtos_conf_comm_channel;
    prtos_s32_t port;

    partition = get_partition(processor_info->sched.current_kthread)->cfg;

    if (OBJDESC_GET_PARTITIONID(desc) != partition->id) return PRTOS_PERM_ERROR;

    if (check_gp_aram(info->port_name, CONFIG_ID_STRING_LENGTH, 1, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    // Look for the channel
    for (port = partition->comm_ports_offset; port < (partition->num_of_ports + partition->comm_ports_offset); port++)
        if (!strcmp(info->port_name, &prtos_conf_string_tab[prtos_conf_comm_ports[port].name_offset])) break;

    if (port >= prtos_conf_table.num_of_comm_ports) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].type != PRTOS_QUEUING_PORT) return PRTOS_INVALID_PARAM;

    if (prtos_conf_comm_ports[port].channel_id != PRTOS_NULL_CHANNEL) {
        prtos_conf_comm_channel = &prtos_conf_comm_channel_table[prtos_conf_comm_ports[port].channel_id];
        info->max_num_of_msgs = prtos_conf_comm_channel->q.max_num_of_msgs;
        info->max_msg_size = prtos_conf_comm_channel->q.max_length;
        info->direction = prtos_conf_comm_ports[port].direction;
    } else {
        info->max_num_of_msgs = 0;
        info->max_msg_size = 0;
        info->direction = 0;
    }

    return PRTOS_OK;
}

static prtos_s32_t ctrl_queuing_port(prtos_obj_desc_t desc, prtos_u32_t cmd, union queuing_port_cmd *__g_param args) {
    if (check_gp_aram(args, sizeof(union queuing_port_cmd), 4, PFLAG_NOT_NULL | PFLAG_RW) < 0) return PRTOS_INVALID_PARAM;

    switch (cmd) {
        case PRTOS_COMM_CREATE_PORT:
            if (!args->create.port_name || (check_gp_aram(args->create.port_name, CONFIG_ID_STRING_LENGTH, 1, PFLAG_NOT_NULL | PFLAG_RW) < 0)) {
                return PRTOS_INVALID_PARAM;
            }
            return create_queuing_port(desc, args->create.port_name, args->create.max_num_of_msgs, args->create.max_msg_size, args->create.direction);
        case PRTOS_COMM_GET_PORT_STATUS:
            return get_queuling_port_status(desc, &args->status);
        case PRTOS_COMM_GET_PORT_INFO:
            return get_queuling_port_info(desc, &args->info);
    }
    return PRTOS_INVALID_PARAM;
}

static const struct object queuing_port_obj = {
    .read = (read_obj_op_t)receive_queuing_port,
    .write = (write_obj_op_t)send_queuing_port,
    .ctrl = (ctrl_obj_op_t)ctrl_queuing_port,
};

prtos_s32_t __VBOOT setup_comm(void) {
    prtos_s32_t e, i;

    ASSERT(GET_CPU_ID() == 0);
    GET_MEMZ(channel_table, sizeof(union channel) * prtos_conf_table.num_of_comm_channels);
    GET_MEMZ(port_table, sizeof(struct port) * prtos_conf_table.num_of_comm_ports);

    for (e = 0; e < prtos_conf_table.num_of_comm_ports; e++) port_table[e].lock = SPINLOCK_INIT;

    /* create the channels */
    for (e = 0; e < prtos_conf_table.num_of_comm_channels; e++) {
        switch (prtos_conf_comm_channel_table[e].type) {
            case PRTOS_SAMPLING_CHANNEL:
                GET_MEMZ(channel_table[e].s.buffer, prtos_conf_comm_channel_table[e].s.max_length);
                GET_MEMZ(channel_table[e].s.receiver_table, prtos_conf_comm_channel_table[e].s.num_of_receivers * sizeof(partition_t *));
                GET_MEMZ(channel_table[e].s.receiver_port_table, prtos_conf_comm_channel_table[e].s.num_of_receivers * sizeof(prtos_s32_t));
                channel_table[e].s.lock = SPINLOCK_INIT;
                break;
            case PRTOS_QUEUING_CHANNEL:
                GET_MEMZ(channel_table[e].q.msg_pool, sizeof(struct msg) * prtos_conf_comm_channel_table[e].q.max_num_of_msgs);
                dyn_list_init(&channel_table[e].q.free_msgs);
                dyn_list_init(&channel_table[e].q.recv_msgs);
                for (i = 0; i < prtos_conf_comm_channel_table[e].q.max_num_of_msgs; i++) {
                    GET_MEMZ(channel_table[e].q.msg_pool[i].buffer, prtos_conf_comm_channel_table[e].q.max_length);
                    if (dyn_list_insert_head(&channel_table[e].q.free_msgs, &channel_table[e].q.msg_pool[i].list_node)) {
                        cpu_ctxt_t ctxt;
                        get_cpu_ctxt(&ctxt);
                        system_panic(&ctxt, "[setup_comm] queuing channels initialization error");
                    }
                }
                channel_table[e].q.lock = SPINLOCK_INIT;
                break;
        }
    }

    object_table[OBJ_CLASS_SAMPLING_PORT] = &sampling_port_obj;
    object_table[OBJ_CLASS_QUEUING_PORT] = &queuing_port_obj;
    return 0;
}

REGISTER_OBJ(setup_comm);
