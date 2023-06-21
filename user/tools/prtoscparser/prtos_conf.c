/*
 * FILE: prtos_conf.c
 *
 * prtos conf implementation
 *
 * www.prtos.org
 */

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "limits.h"
#include "common.h"
#include "parser.h"
#include "prtos_conf.h"
#include "checks.h"

struct prtos_conf prtos_conf;
struct prtos_conf_line_number prtos_conf_line_number;
struct prtos_conf_memory_region *prtos_conf_mem_reg_table = 0;
struct prtos_conf_memory_region_line_number *prtos_conf_mem_reg_table_line_number = 0;
struct prtos_conf_memory_area *prtos_conf_mem_area_table = 0;
struct prtos_conf_memory_area_line_number *prtos_conf_mem_area_table_line_number = 0;
struct prtos_conf_sched_cyclic_slot *prtos_conf_sched_cyclic_slot_table = 0;
struct prtos_conf_sched_cyclic_slot_line_number *prtos_conf_sched_cyclic_slot_table_line_number = 0;
struct prtos_conf_part *prtos_conf_partition_table = 0;
struct prtos_conf_part_line_number *prtos_conf_partition_table_line_number = 0;
struct prtos_conf_io_port *prtos_conf_io_port_table = 0;
struct prtos_conf_io_port_code_line_number *prtos_conf_io_port_table_line_number = 0;
struct prtos_conf_comm_port *prtos_conf_comm_port_table = 0;
struct prtos_conf_comm_port_code_line_number *prtos_conf_comm_port_table_line_number = 0;
struct prtos_conf_comm_channel *prtos_conf_comm_channel_table = 0;
struct prtos_conf_comm_channel_line_number *prtos_conf_comm_channel_table_line_number = 0;
struct prtos_conf_mem_block *prtos_conf_mem_block_table = 0;
struct prtos_conf_mem_block_line_number *prtos_conf_mem_block_table_line_number = 0;
struct ipcPort *ipc_port_table = 0;
struct ipc_port_line_number *ipc_port_table_line_number = 0;
struct prtos_conf_sched_cyclic_plan *prtos_conf_sched_cyclic_plan_table = 0;
struct prtos_conf_sched_cyclic_plan_line_number *prtos_conf_sched_cyclic_plan_table_line_number = 0;
struct src_ipvi *src_ipvi_table = 0;
struct src_ipvi_line_number *src_ipvi_table_line_number = 0;
int num_of_src_ipvi = 0;
char *ipvi_dst_table = 0;
int num_of_ipc_ports = 0;

char *str_tables = NULL;

static struct device_table {
    char *name;
    prtos_dev_t id;
    int line;
} *device_table = 0;

static int device_table_length = 0;

prtos_dev_t look_up_device(char *name, int line) {
    int e;
    for (e = 0; e < device_table_length; e++)
        if (!strcmp(name, device_table[e].name)) return device_table[e].id;
    line_error(line, "\"%s\" not found in the device table", name);
    return (prtos_dev_t){.id = PRTOS_DEV_INVALID_ID, .sub_id = 0};
}

void register_device(char *name, prtos_dev_t id, int line) {
    int e;
    for (e = 0; e < device_table_length; e++)
        if (!strcmp(name, device_table[e].name)) line_error(line, "device name already registered (line %d)", device_table[e].line);

    e = device_table_length++;
    DO_REALLOC(device_table, device_table_length * sizeof(struct device_table));
    device_table[e].name = strdup(name);
    device_table[e].id = id;
    device_table[e].line = line;
}

static char **dev_name_table = 0;
static int dev_name_table_length = 0;

int add_dev_name(char *name) {
    int e;
    if (!strcmp(name, "NULL")) return PRTOS_DEV_INVALID_ID;
    e = dev_name_table_length++;
    DO_REALLOC(dev_name_table, sizeof(char *) * dev_name_table_length);
    dev_name_table[e] = strdup(name);
    return e;
}

void link_devices(void) {
#define IS_DEFINED(_x) (((_x).id != PRTOS_DEV_INVALID_ID) && ((_x).id < dev_name_table_length))
    int e;
    if (IS_DEFINED(prtos_conf.hpv.console_device))
        prtos_conf.hpv.console_device = look_up_device(dev_name_table[prtos_conf.hpv.console_device.id], prtos_conf_line_number.hpv.console_device);
    if (IS_DEFINED(prtos_conf.hpv.trace.dev))
        prtos_conf.hpv.trace.dev = look_up_device(dev_name_table[prtos_conf.hpv.trace.dev.id], prtos_conf_line_number.hpv.trace.dev);
    if (IS_DEFINED(prtos_conf.hpv.hm_device))
        prtos_conf.hpv.hm_device = look_up_device(dev_name_table[prtos_conf.hpv.hm_device.id], prtos_conf_line_number.hpv.hm_device);

    for (e = 0; e < prtos_conf.num_of_partitions; e++) {
        if (IS_DEFINED(prtos_conf_partition_table[e].console_device))
            prtos_conf_partition_table[e].console_device =
                look_up_device(dev_name_table[prtos_conf_partition_table[e].console_device.id], prtos_conf_partition_table_line_number[e].console_device);

        if (IS_DEFINED(prtos_conf_partition_table[e].trace.dev))
            prtos_conf_partition_table[e].trace.dev =
                look_up_device(dev_name_table[prtos_conf_partition_table[e].trace.dev.id], prtos_conf_partition_table_line_number[e].trace.dev);
    }
}

prtos_u32_t add_string(char *s) {
    prtos_u32_t offset;
    offset = prtos_conf.string_table_length;
    prtos_conf.string_table_length += strlen(s) + 1;
    DO_REALLOC(str_tables, prtos_conf.string_table_length * sizeof(char));
    strcpy(&str_tables[offset], s);
    return offset;
}

void link_channels_to_ports(void) {
    struct prtos_conf_comm_port *port = 0;
    int e, i;

    for (i = 0; i < num_of_ipc_ports; i++) {
        if (ipc_port_table[i].partition_id >= prtos_conf.num_of_partitions)
            line_error(ipc_port_table_line_number[i].partition_id, "incorrect partition id (%d)", ipc_port_table[i].partition_id);
        if (ipc_port_table[i].partition_name &&
            strcmp(ipc_port_table[i].partition_name, &str_tables[prtos_conf_partition_table[ipc_port_table[i].partition_id].name_offset]))
            line_error(ipc_port_table_line_number[i].partition_name, "partition name \"%s\" mismatches with the expected one \"%s\"",
                       ipc_port_table[i].partition_name, &str_tables[prtos_conf_partition_table[ipc_port_table[i].partition_id].name_offset]);
        for (e = 0; e < prtos_conf_partition_table[ipc_port_table[i].partition_id].num_of_ports; e++) {
            port = &prtos_conf_comm_port_table[prtos_conf_partition_table[ipc_port_table[i].partition_id].comm_ports_offset + e];
            if (!strcmp(ipc_port_table[i].portName, &str_tables[port->name_offset])) {
                port->channel_id = ipc_port_table[i].channel;
                if ((prtos_conf_comm_channel_table[port->channel_id].type == PRTOS_SAMPLING_CHANNEL) && (port->direction == PRTOS_DESTINATION_PORT))
                    prtos_conf_comm_channel_table[port->channel_id].s.num_of_receivers++;
                break;
            }
        }

        if (e >= prtos_conf_partition_table[ipc_port_table[i].partition_id].num_of_ports)
            line_error(ipc_port_table_line_number[i].portName, "port \"%s\" not found", ipc_port_table[i].portName);
        if (prtos_conf_comm_channel_table[ipc_port_table[i].channel].type != port->type)
            line_error(prtos_conf_comm_channel_table_line_number[ipc_port_table[i].channel].type, "channel type mismatches with the type of the port");
        if (ipc_port_table[i].direction != port->direction)
            line_error(ipc_port_table_line_number[i].direction, "channel direction mismatches with the direction of the port");
    }
}

void setup_hw_irq_mask(void) {
    int e, i;
    for (e = 0; e < prtos_conf.num_of_partitions; e++) {
        for (i = 0; i < CONFIG_NO_HWIRQS; i++) {
            if (prtos_conf.hpv.hw_irq_table[i].owner == prtos_conf_partition_table[e].id) prtos_conf_partition_table[e].hw_irqs |= (1 << i);
        }
    }
}

static int cmp_mem_area(struct prtos_conf_memory_area *m1, struct prtos_conf_memory_area *m2) {
    if (m1->start_addr > m2->start_addr) return 1;
    if (m1->start_addr < m2->start_addr) return -1;
    return 0;
}

void sort_phys_mem_areas(void) {
    prtos_s32_t e;
    for (e = 0; e < prtos_conf.num_of_partitions; e++) {
        qsort(&prtos_conf_mem_area_table[prtos_conf_partition_table[e].physical_memory_areas_offset], prtos_conf_partition_table[e].num_of_physical_memory_areas,
              sizeof(struct prtos_conf_memory_area), (int (*)(const void *, const void *))cmp_mem_area);
        check_all_mem_areas(&prtos_conf_mem_area_table[prtos_conf_partition_table[e].physical_memory_areas_offset],
                            &prtos_conf_mem_area_table_line_number[prtos_conf_partition_table[e].physical_memory_areas_offset],
                            prtos_conf_partition_table[e].num_of_physical_memory_areas);
    }
}

static int cmp_mem_regions(struct prtos_conf_memory_region *m1, struct prtos_conf_memory_region *m2) {
    if (m1->start_addr > m2->start_addr) return 1;
    if (m1->start_addr < m2->start_addr) return -1;
    return 0;
}

void sort_mem_regions(void) {
    qsort(prtos_conf_mem_reg_table, prtos_conf.num_of_regions, sizeof(struct prtos_conf_memory_region), (int (*)(const void *, const void *))cmp_mem_regions);
    check_all_memreg();
}

void process_ipvi_table(void) {
    int e, i, j;
    struct src_ipvi *src;
    struct dst_ipvi *dst;

    for (e = 0; e < prtos_conf.num_of_partitions; e++) {
        memset(&prtos_conf_partition_table[e].ipvi_table, 0, sizeof(struct prtos_conf_part_ipvi) * CONFIG_PRTOS_MAX_IPVI);
        for (src = 0, i = 0; i < num_of_src_ipvi; i++) {
            src = &src_ipvi_table[i];
            if (prtos_conf_partition_table[e].id == src->id) {
                prtos_conf_partition_table[e].ipvi_table[src->ipvi_id].dst_offset = prtos_conf.num_of_ipvi_dsts;
                for (j = 0; j < src->num_of_dsts; j++) {
                    dst = &src->dst[j];
                    prtos_conf.num_of_ipvi_dsts++;
                    prtos_conf_partition_table[e].ipvi_table[src->ipvi_id].num_of_dsts++;
                    DO_REALLOC(ipvi_dst_table, prtos_conf.num_of_ipvi_dsts);
                    ipvi_dst_table[prtos_conf.num_of_ipvi_dsts - 1] = dst->id;
                }
            }
        }
    }
}
