/*
 * FILE: process_xml.c
 *
 * www.prtos.org
 */

#include <string.h>
#include "common.h"
#include "parser.h"
#include "conv.h"
#include "checks.h"
#include "prtos_conf.h"

static int count_partition = 0;
static int expected_slot_number = 0, count_proccess_cyclic_plan = 0;

static void name_mem_area_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_mem_area_table[C_PHYSMEMAREA].name_offset = add_string((char *)val);
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].name = node->line;
    prtos_conf_mem_area_table[C_PHYSMEMAREA].flags |= PRTOS_MEM_AREA_TAGGED;
}

static struct attr_xml name_mem_area_attr = {BAD_CAST "name", name_mem_area_attr_handle};

static void start_mem_area_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_mem_area_table[C_PHYSMEMAREA].start_addr = to_u32((char *)val, 16);
    prtos_conf_mem_area_table[C_PHYSMEMAREA].mapped_at = to_u32((char *)val, 16);

    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].start_addr = node->line;
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].mapped_at = node->line;
}

static struct attr_xml start_mem_area_attr = {BAD_CAST "start", start_mem_area_attr_handle};

static void virt_addr_mem_area_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_mem_area_table[C_PHYSMEMAREA].mapped_at = to_u32((char *)val, 16);
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].mapped_at = node->line;
}

static struct attr_xml mapped_at_mem_area_attr = {BAD_CAST "mappedAt", virt_addr_mem_area_attr_handle};

static void size_mem_area_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_mem_area_table[C_PHYSMEMAREA].size = to_size((char *)val);
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].size = node->line;
}

static struct attr_xml size_mem_area_attr = {BAD_CAST "size", size_mem_area_attr_handle};

static void flags_mem_area_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_mem_area_table[C_PHYSMEMAREA].flags |= to_phys_mem_area_flags((char *)val, node->line);
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].flags = node->line;
}

static struct attr_xml flags_mem_area_attr = {BAD_CAST "flags", flags_mem_area_attr_handle};

static prtos_s32_t *num_of_mem_area_ptr = 0;

static void area_node_handle0(xmlNodePtr node) {
    if (num_of_mem_area_ptr) (*num_of_mem_area_ptr)++;
    prtos_conf.num_of_physical_memory_areas++;
    DO_REALLOC(prtos_conf_mem_area_table, prtos_conf.num_of_physical_memory_areas * sizeof(struct prtos_conf_memory_area));
    DO_REALLOC(prtos_conf_mem_area_table_line_number, prtos_conf.num_of_physical_memory_areas * sizeof(struct prtos_conf_memory_area_line_number));
    memset(&prtos_conf_mem_area_table[C_PHYSMEMAREA], 0, sizeof(struct prtos_conf_memory_area));
    memset(&prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA], 0, sizeof(struct prtos_conf_memory_area_line_number));
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].line = node->line;
    prtos_conf_mem_area_table[C_PHYSMEMAREA].name_offset = 0;
    if (count_partition == -2) {
        prtos_conf_mem_area_table[C_PHYSMEMAREA].start_addr = CONFIG_PRTOS_LOAD_ADDR;
        prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].start_addr = node->line;
        prtos_conf_mem_area_table[C_PHYSMEMAREA].mapped_at = CONFIG_PRTOS_OFFSET;
        prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].mapped_at = node->line;
    }
}

static void area_node_handle1(xmlNodePtr node) {
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].partition_id = count_partition;
    prtos_conf_mem_area_table[C_PHYSMEMAREA].memory_region_offset = check_phys_mem_area(C_PHYSMEMAREA);
}

static struct node_xml area_node = {
    BAD_CAST "Area",
    area_node_handle0,
    area_node_handle1,
    0,
    (struct attr_xml *[]){&name_mem_area_attr, &start_mem_area_attr, &size_mem_area_attr, &flags_mem_area_attr, &mapped_at_mem_area_attr, 0},
    0};

static struct node_xml mem_area_node = {BAD_CAST "PhysicalMemoryAreas", 0, 0, 0, 0, (struct node_xml *[]){&area_node, 0}};

static struct node_xml hyp_mem_area_node = {
    BAD_CAST "PhysicalMemoryArea", area_node_handle0, area_node_handle1, 0, (struct attr_xml *[]){&size_mem_area_attr, &flags_mem_area_attr, 0}, 0};

static int hm_slot_number = 0, hm_slot_line = 0;
static struct prtos_conf_hm_slot *hm_table_ptr = 0;

static void name_hm_event_attr_handle(xmlNodePtr node, const xmlChar *val) {
    hm_slot_number = to_hm_event((char *)val, node->line);
}

static struct attr_xml name_hm_event_attr = {BAD_CAST "name", name_hm_event_attr_handle};

static void action_hm_event_attr_handle(xmlNodePtr node, const xmlChar *val) {
    hm_table_ptr[hm_slot_number].action = to_hm_action((char *)val, node->line);
}

static struct attr_xml action_hm_event_attr = {BAD_CAST "action", action_hm_event_attr_handle};

static void log_hm_event_attr_handle(xmlNodePtr node, const xmlChar *val) {
    hm_table_ptr[hm_slot_number].log = to_yes_no_true_false((char *)val, node->line);
}

static struct attr_xml log_hm_event_attr = {BAD_CAST "log", log_hm_event_attr_handle};

static void event_hm_node_handle0(xmlNodePtr node) {
    hm_slot_line = node->line;
    if (count_partition < 0)
        hm_hpv_is_action_permitted_on_event(hm_slot_number, hm_table_ptr[hm_slot_number].action, hm_slot_line);
    else
        hm_part_is_action_permitted_on_event(hm_slot_number, hm_table_ptr[hm_slot_number].action, hm_slot_line);
}

static void event_hm_node_handle1(xmlNodePtr node) {
    if (count_partition < 0)
        hm_hpv_is_action_permitted_on_event(hm_slot_number, hm_table_ptr[hm_slot_number].action, hm_slot_line);
    else
        hm_part_is_action_permitted_on_event(hm_slot_number, hm_table_ptr[hm_slot_number].action, hm_slot_line);
}

static struct node_xml event_hm_node = {BAD_CAST "Event",
                                        event_hm_node_handle0,
                                        event_hm_node_handle1,
                                        0,
                                        (struct attr_xml *[]){&name_hm_event_attr, &action_hm_event_attr, &log_hm_event_attr, 0},
                                        0};

static struct node_xml hm_node = {BAD_CAST "HealthMonitor", 0, 0, 0, 0, (struct node_xml *[]){&event_hm_node, 0}};

static struct prtos_conf_trace *trace_ptr = 0;
static struct prtos_conf_trace_line_number *trace_line_number_ptr = 0;

static void device_trace_attr_handle(xmlNodePtr node, const xmlChar *val) {
    trace_ptr->dev.id = add_dev_name((char *)val);
    trace_line_number_ptr->dev = node->line;
}

static struct attr_xml device_trace_attr = {BAD_CAST "device", device_trace_attr_handle};

static void bitmask_trace_attr_handle(xmlNodePtr node, const xmlChar *val) {
    trace_ptr->bitmap = to_u32((char *)val, 16);
}

static struct attr_xml bitmask_trace_attr = {BAD_CAST "bitmask", bitmask_trace_attr_handle};

static struct node_xml trace_node = {BAD_CAST "Trace", 0, 0, 0, (struct attr_xml *[]){&device_trace_attr, &bitmask_trace_attr, 0}, 0};

static void bitmask_traceHyp_attr_handle(xmlNodePtr node, const xmlChar *val) {
    trace_ptr->bitmap = to_bitmask_trace_hyp((char *)val, node->line);
}

static struct attr_xml bitmask_trace_hyp_attr = {BAD_CAST "bitmask", bitmask_traceHyp_attr_handle};

static struct node_xml trace_hyp_node = {BAD_CAST "Trace", 0, 0, 0, (struct attr_xml *[]){&device_trace_attr, &bitmask_trace_hyp_attr, 0}, 0};

static void id_slot_attr_handle(xmlNodePtr node, const xmlChar *val) {
    int d = to_u32((char *)val, 10);

    if (d != expected_slot_number) line_error(node->line, "slot id (%d) shall be consecutive starting at 0", d);
    expected_slot_number++;
    prtos_conf_sched_cyclic_slot_table[C_CYCLICSLOT].id = d;
    prtos_conf_sched_cyclic_slot_table_line_number[C_CYCLICSLOT].id = node->line;
}

static struct attr_xml id_slot_attr = {BAD_CAST "id", id_slot_attr_handle};

static void vcpu_id_slot_attr_handle(xmlNodePtr node, const xmlChar *val) {
    int d = to_u32((char *)val, 10);

    prtos_conf_sched_cyclic_slot_table[C_CYCLICSLOT].vcpu_id = d;
    prtos_conf_sched_cyclic_slot_table_line_number[C_CYCLICSLOT].vCpuId = node->line;
}

static struct attr_xml vpuc_id_slot_attr = {BAD_CAST "vCpuId", vcpu_id_slot_attr_handle};

static void start_slot_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_sched_cyclic_slot_table[C_CYCLICSLOT].start_exec = to_time((char *)val);
    prtos_conf_sched_cyclic_slot_table_line_number[C_CYCLICSLOT].start_exec = node->line;
}

static struct attr_xml start_slot_attr = {BAD_CAST "start", start_slot_attr_handle};

static void duration_slot_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_sched_cyclic_slot_table[C_CYCLICSLOT].end_exec = to_time((char *)val);
    prtos_conf_sched_cyclic_slot_table_line_number[C_CYCLICSLOT].end_exec = node->line;
}

static struct attr_xml duration_slot_attr = {BAD_CAST "duration", duration_slot_attr_handle};

static void Partitionid_slot_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_sched_cyclic_slot_table[C_CYCLICSLOT].partition_id = to_u32((char *)val, 10);
    prtos_conf_sched_cyclic_slot_table_line_number[C_CYCLICSLOT].partition_id = node->line;
}

static struct attr_xml partition_id_slot_attr = {BAD_CAST "partitionId", Partitionid_slot_attr_handle};

static void slot_node_handle0(xmlNodePtr node) {
    prtos_conf_sched_cyclic_plan_table[C_CYCLICPLAN].num_of_slots++;
    prtos_conf.num_of_sched_cyclic_slots++;
    DO_REALLOC(prtos_conf_sched_cyclic_slot_table, prtos_conf.num_of_sched_cyclic_slots * sizeof(struct prtos_conf_sched_cyclic_slot));
    DO_REALLOC(prtos_conf_sched_cyclic_slot_table_line_number, prtos_conf.num_of_sched_cyclic_slots * sizeof(struct prtos_conf_sched_cyclic_slot_line_number));
    memset(&prtos_conf_sched_cyclic_slot_table[C_CYCLICSLOT], 0, sizeof(struct prtos_conf_sched_cyclic_slot));
    memset(&prtos_conf_sched_cyclic_slot_table_line_number[C_CYCLICSLOT], 0, sizeof(struct prtos_conf_sched_cyclic_slot_line_number));
}

static void slot_node_handle1(xmlNodePtr node) {
    prtos_conf_sched_cyclic_slot_table[C_CYCLICSLOT].end_exec += prtos_conf_sched_cyclic_slot_table[C_CYCLICSLOT].start_exec;
}

static struct node_xml slot_node = {BAD_CAST "Slot",
                                    slot_node_handle0,
                                    slot_node_handle1,
                                    0,
                                    (struct attr_xml *[]){&id_slot_attr, &vpuc_id_slot_attr, &start_slot_attr, &duration_slot_attr, &partition_id_slot_attr, 0},
                                    0};

static void name_cyclic_plan_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_sched_cyclic_plan_table[C_CYCLICPLAN].name_offset = add_string((char *)val);
    prtos_conf_sched_cyclic_plan_table_line_number[C_CYCLICPLAN].name = node->line;
}

static struct attr_xml name_cyclic_plan_attr = {BAD_CAST "name", name_cyclic_plan_attr_handle};

static void id_cyclic_plan_attr_handle(xmlNodePtr node, const xmlChar *val) {
    int d = to_u32((char *)val, 10);
    if (d != (count_proccess_cyclic_plan - 1)) line_error(node->line, "Cyclic plan id (%d) shall be consecutive starting at 0", d);

    prtos_conf_sched_cyclic_plan_table[C_CYCLICPLAN].id = d;
}

static struct attr_xml id_cyclic_plan_attr = {BAD_CAST "id", id_cyclic_plan_attr_handle};

static void major_frame_cyclic_plan_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_sched_cyclic_plan_table[C_CYCLICPLAN].major_frame = to_time((char *)val);
    prtos_conf_sched_cyclic_plan_table_line_number[C_CYCLICPLAN].major_frame = node->line;
}

static struct attr_xml major_frame_cyclic_plan_attr = {BAD_CAST "majorFrame", major_frame_cyclic_plan_attr_handle};

static void plan_node_handle0(xmlNodePtr node) {
    count_proccess_cyclic_plan++;
    prtos_conf.num_of_sched_cyclic_plans++;
    prtos_conf.hpv.cpu_table[C_CPU].num_of_sched_cyclic_plans++;
    prtos_conf_line_number.hpv.cpu_table[C_CPU].plan = node->line;
    DO_REALLOC(prtos_conf_sched_cyclic_plan_table, prtos_conf.num_of_sched_cyclic_plans * sizeof(struct prtos_conf_sched_cyclic_plan));
    DO_REALLOC(prtos_conf_sched_cyclic_plan_table_line_number, prtos_conf.num_of_sched_cyclic_plans * sizeof(struct prtos_conf_sched_cyclic_plan_line_number));
    memset(&prtos_conf_sched_cyclic_plan_table[C_CYCLICPLAN], 0, sizeof(struct prtos_conf_sched_cyclic_plan));
    memset(&prtos_conf_sched_cyclic_plan_table_line_number[C_CYCLICPLAN], 0, sizeof(struct prtos_conf_sched_cyclic_plan_line_number));
    prtos_conf_sched_cyclic_plan_table[C_CYCLICPLAN].slots_offset = prtos_conf.num_of_sched_cyclic_slots;
    expected_slot_number = 0;
}

static void plan_node_hangle2(xmlNodePtr node) {
    check_sched_cyclic_plan(&prtos_conf_sched_cyclic_plan_table[C_CYCLICPLAN], &prtos_conf_sched_cyclic_plan_table_line_number[C_CYCLICPLAN]);
}

static struct node_xml plan_node = {BAD_CAST "Plan",
                                    plan_node_handle0,
                                    0,
                                    plan_node_hangle2,
                                    (struct attr_xml *[]){&name_cyclic_plan_attr, &id_cyclic_plan_attr, &major_frame_cyclic_plan_attr, 0},
                                    (struct node_xml *[]){&slot_node, 0}};

static void cyclic_plan_node_handle0(xmlNodePtr node) {
    prtos_conf.hpv.cpu_table[C_CPU].sched_cyclic_plans_offset = prtos_conf.num_of_sched_cyclic_plans;
}

static struct node_xml cyclic_plan_node = {BAD_CAST "CyclicPlanTable", cyclic_plan_node_handle0, 0, 0, 0, (struct node_xml *[]){&plan_node, 0}};

static void id_processor_attr_handle(xmlNodePtr node, const xmlChar *val) {
    int d = to_u32((char *)val, 10);
    if (d != C_CPU) line_error(node->line, "processor id (%d) shall be consecutive starting at 0", d);
    prtos_conf.hpv.cpu_table[C_CPU].id = d;
    prtos_conf_line_number.hpv.cpu_table[C_CPU].id = node->line;
}

static struct attr_xml id_processor_attr = {BAD_CAST "id", id_processor_attr_handle};

static void frequency_processor_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf.hpv.cpu_table[C_CPU].freq = to_freq((char *)val);
    prtos_conf_line_number.hpv.cpu_table[C_CPU].freq = node->line;
}

static struct attr_xml frequency_processor_attr = {BAD_CAST "frequency", frequency_processor_attr_handle};

static void features_processor_attr_handle(xmlNodePtr node, const xmlChar *val) {}

static struct attr_xml features_processor_attr = {BAD_CAST "features", features_processor_attr_handle};

static void processor_node_handle0(xmlNodePtr node) {
    prtos_conf.hpv.num_of_cpus++;
    count_proccess_cyclic_plan = 0;
    if (prtos_conf.hpv.num_of_cpus > CONFIG_NO_CPUS) line_error(node->line, "no more than %d cpus are supported", CONFIG_NO_CPUS);
}

static struct node_xml processor_node = {BAD_CAST "Processor",
                                         processor_node_handle0,
                                         0,
                                         0,
                                         (struct attr_xml *[]){&id_processor_attr, &frequency_processor_attr, &features_processor_attr, 0},
                                         (struct node_xml *[]){&cyclic_plan_node, 0}};

static struct node_xml processor_table_node = {BAD_CAST "ProcessorTable", 0, 0, 0, 0, (struct node_xml *[]){&processor_node, 0}};

static void type_mem_reg_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_mem_reg_table[C_REGION].flags = to_region_flags((char *)val);
    prtos_conf_mem_reg_table_line_number[C_REGION].flags = node->line;
}

static struct attr_xml type_mem_teg_attr = {BAD_CAST "type", type_mem_reg_attr_handle};

static void start_mem_reg_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_mem_reg_table[C_REGION].start_addr = to_u32((char *)val, 16);
    prtos_conf_mem_reg_table_line_number[C_REGION].start_addr = node->line;
}

static struct attr_xml start_mem_reg_attr = {BAD_CAST "start", start_mem_reg_attr_handle};

static void size_mem_reg_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_mem_reg_table[C_REGION].size = to_size((char *)val);
    prtos_conf_mem_reg_table_line_number[C_REGION].size = node->line;
}

static struct attr_xml size_mem_reg_attr = {BAD_CAST "size", size_mem_reg_attr_handle};

static void region_node_handle0(xmlNodePtr node) {
    prtos_conf.num_of_regions++;
    DO_REALLOC(prtos_conf_mem_reg_table, prtos_conf.num_of_regions * sizeof(struct prtos_conf_memory_region));
    DO_REALLOC(prtos_conf_mem_reg_table_line_number, prtos_conf.num_of_regions * sizeof(struct prtos_conf_memory_region_line_number));
    prtos_conf_mem_reg_table_line_number[C_REGION].line = node->line;
}

static void region_node_handle1(xmlNodePtr node) {
    check_memory_region(C_REGION);
}

static struct node_xml region_node = {
    BAD_CAST "Region", region_node_handle0, region_node_handle1, 0, (struct attr_xml *[]){&type_mem_teg_attr, &start_mem_reg_attr, &size_mem_reg_attr, 0}, 0};

static struct node_xml memory_layout_node = {BAD_CAST "MemoryLayout", 0, 0, 0, 0, (struct node_xml *[]){&region_node, 0}};

#if defined(CONFIG_DEV_MEMBLOCK) || defined(CONFIG_DEV_MEMBLOCK_MODULE)
static void name_mem_block_dev_attr_handle(xmlNodePtr node, const xmlChar *val) {
    register_device((char *)val, (prtos_dev_t){PRTOS_DEV_LOGSTORAGE_ID, prtos_conf.device_table.num_of_mem_blocks - 1}, node->line);
}

static struct attr_xml name_mem_block_dev_attr = {BAD_CAST "name", name_mem_block_dev_attr_handle};

static void start_mem_block_dev_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_mem_area_table[C_PHYSMEMAREA].flags = PRTOS_MEM_AREA_SHARED;
    prtos_conf_mem_area_table[C_PHYSMEMAREA].start_addr = to_u32((char *)val, 16);
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].start_addr = node->line;
}

static struct attr_xml start_mem_block_dev_attr = {BAD_CAST "start", start_mem_block_dev_attr_handle};

static void size_mem_block_dev_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_mem_area_table[C_PHYSMEMAREA].size = to_size((char *)val);
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].size = node->line;
}

static struct attr_xml size_mem_block_dev_attr = {BAD_CAST "size", size_mem_block_dev_attr_handle};

static void memory_block_device_node_handle0(xmlNodePtr node) {
    prtos_conf.device_table.num_of_mem_blocks++;
    DO_REALLOC(prtos_conf_mem_block_table, prtos_conf.device_table.num_of_mem_blocks * sizeof(struct prtos_conf_mem_block));
    DO_REALLOC(prtos_conf_mem_block_table_line_number, prtos_conf.device_table.num_of_mem_blocks * sizeof(struct prtos_conf_mem_block_line_number));
    memset(&prtos_conf_mem_block_table[C_MEMORYBLOCK_DEV], 0, sizeof(struct prtos_conf_mem_block));
    memset(&prtos_conf_mem_block_table_line_number[C_MEMORYBLOCK_DEV], 0, sizeof(struct prtos_conf_mem_block_line_number));
    prtos_conf_mem_block_table_line_number[C_MEMORYBLOCK_DEV].line = node->line;
    prtos_conf_mem_block_table[C_MEMORYBLOCK_DEV].physical_memory_areas_offset = prtos_conf.num_of_physical_memory_areas;

    prtos_conf.num_of_physical_memory_areas++;
    DO_REALLOC(prtos_conf_mem_area_table, prtos_conf.num_of_physical_memory_areas * sizeof(struct prtos_conf_memory_area));
    DO_REALLOC(prtos_conf_mem_area_table_line_number, prtos_conf.num_of_physical_memory_areas * sizeof(struct prtos_conf_memory_area_line_number));
    memset(&prtos_conf_mem_area_table[C_PHYSMEMAREA], 0, sizeof(struct prtos_conf_memory_area));
    memset(&prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA], 0, sizeof(struct prtos_conf_memory_area_line_number));
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].line = node->line;
}

static void memory_block_device_node_handle1(xmlNodePtr node) {
    // CheckMemBlock(C_MEMORYBLOCK_DEV);
    prtos_conf_mem_area_table_line_number[C_PHYSMEMAREA].partition_id = count_partition;
    prtos_conf_mem_area_table[C_PHYSMEMAREA].memory_region_offset = check_phys_mem_area(C_PHYSMEMAREA);
}

static struct node_xml memory_block_device_node = {BAD_CAST "MemoryBlock",
                                                   memory_block_device_node_handle0,
                                                   memory_block_device_node_handle1,
                                                   0,
                                                   (struct attr_xml *[]){&name_mem_block_dev_attr, &start_mem_block_dev_attr, &size_mem_block_dev_attr, 0},
                                                   0};
#endif
#if defined(CONFIG_DEV_UART) || defined(CONFIG_DEV_UART_MODULE)
static int uart_id;

static void id_uart_attr_handle(xmlNodePtr node, const xmlChar *val) {
    uart_id = to_u32((char *)val, 10);
    check_uart_id(uart_id, node->line);
}

static struct attr_xml id_uart_attr = {BAD_CAST "id", id_uart_attr_handle};

static void name_uart_attr_handle(xmlNodePtr node, const xmlChar *val) {
    register_device((char *)val, (prtos_dev_t){PRTOS_DEV_UART_ID, uart_id}, node->line);
}

static struct attr_xml name_uart_attr = {BAD_CAST "name", name_uart_attr_handle};

static void baud_rate_uart_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf.device_table.uart[uart_id].baud_rate = to_u32((char *)val, 10);
}

static struct attr_xml baud_rate_uart_attr = {BAD_CAST "baudRate", baud_rate_uart_attr_handle};

static struct node_xml uart_device_node = {BAD_CAST "Uart", 0, 0, 0, (struct attr_xml *[]){&id_uart_attr, &name_uart_attr, &baud_rate_uart_attr, 0}, 0};
#endif

#ifdef CONFIG_x86
static void name_vga_attr_handle(xmlNodePtr node, const xmlChar *val) {
    register_device((char *)val, (prtos_dev_t){PRTOS_DEV_VGA_ID, 0}, node->line);
}

static struct attr_xml name_vga_attr = {BAD_CAST "name", name_vga_attr_handle};

static struct node_xml vga_device_node = {BAD_CAST "Vga", 0, 0, 0, (struct attr_xml *[]){&name_vga_attr, 0}, 0};
#endif

static void devices_node_handle0(xmlNodePtr node) {
    register_device((char *)"Null", (prtos_dev_t){PRTOS_DEV_INVALID_ID, 0}, node->line);
}

static struct node_xml devices_node = {BAD_CAST "Devices",
                                       devices_node_handle0,
                                       0,
                                       0,
                                       0,
                                       (struct node_xml *[]){
#if defined(CONFIG_DEV_MEMBLOCK) || defined(CONFIG_DEV_MEMBLOCK_MODULE)
                                           &memory_block_device_node,
#endif
#if defined(CONFIG_DEV_UART) || defined(CONFIG_DEV_UART_MODULE)
                                           &uart_device_node,
#endif
#ifdef CONFIG_x86
                                           &vga_device_node,
#endif
                                           0}};

static void partition_id_ipc_attr_handle(xmlNodePtr node, const xmlChar *val) {
    ipc_port_table[C_IPCPORT].partition_id = to_u32((char *)val, 10);
    ipc_port_table_line_number[C_IPCPORT].partition_id = node->line;
}

static struct attr_xml partition_id_ipc_attr = {BAD_CAST "partitionId", partition_id_ipc_attr_handle};

static void partition_name_ipc_attr_handle(xmlNodePtr node, const xmlChar *val) {
    ipc_port_table[C_IPCPORT].partition_name = strdup((char *)val);
    ipc_port_table_line_number[C_IPCPORT].partition_name = node->line;
}

static struct attr_xml partition_name_ipc_attr = {BAD_CAST "partitionName", partition_name_ipc_attr_handle};

static void PortNameIpc_attr_handle(xmlNodePtr node, const xmlChar *val) {
    ipc_port_table[C_IPCPORT].portName = strdup((char *)val);
    ipc_port_table_line_number[C_IPCPORT].portName = node->line;
}
static struct attr_xml port_name_ipc_attr = {BAD_CAST "portName", PortNameIpc_attr_handle};

static void ipc_port_node_handle0(xmlNodePtr node) {
    num_of_ipc_ports++;
    DO_REALLOC(ipc_port_table, num_of_ipc_ports * sizeof(struct ipcPort));
    DO_REALLOC(ipc_port_table_line_number, num_of_ipc_ports * sizeof(struct ipc_port_line_number));
    memset(&ipc_port_table[C_IPCPORT], 0, sizeof(struct ipcPort));
    memset(&ipc_port_table_line_number[C_IPCPORT], 0, sizeof(struct ipc_port_line_number));
    ipc_port_table[C_IPCPORT].channel = C_COMM_CHANNEL;
}

static void source_ipc_port_node_handle1(xmlNodePtr node) {
    ipc_port_table[C_IPCPORT].direction = PRTOS_SOURCE_PORT;
}

static struct node_xml source_ipc_port_node = {BAD_CAST "Source",
                                               ipc_port_node_handle0,
                                               source_ipc_port_node_handle1,
                                               0,
                                               (struct attr_xml *[]){&partition_id_ipc_attr, &partition_name_ipc_attr, &port_name_ipc_attr, 0},
                                               0};

static void destination_ipc_port_node_handle1(xmlNodePtr node) {
    ipc_port_table[C_IPCPORT].direction = PRTOS_DESTINATION_PORT;
}

static struct node_xml destination_ipc_port_node = {BAD_CAST "Destination",
                                                    ipc_port_node_handle0,
                                                    destination_ipc_port_node_handle1,
                                                    0,
                                                    (struct attr_xml *[]){&partition_id_ipc_attr, &partition_name_ipc_attr, &port_name_ipc_attr, 0},
                                                    0};

static void max_message_length_sampling_channel_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_comm_channel_table[C_COMM_CHANNEL].s.max_length = to_size((char *)val);
    prtos_conf_comm_channel_table_line_number[C_COMM_CHANNEL].s.max_length = node->line;
}

static struct attr_xml max_message_length_samping_channel_attr = {BAD_CAST "maxMessageLength", max_message_length_sampling_channel_attr_handle};

static void valid_period_sampling_channel_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_comm_channel_table[C_COMM_CHANNEL].s.valid_period = to_time((char *)val);
}

static struct attr_xml valid_period_sampling_channel_attr = {BAD_CAST "validPeriod", valid_period_sampling_channel_attr_handle};

static void sampling_channel_node_handle0(xmlNodePtr node) {
    prtos_conf.num_of_comm_channels++;
    DO_REALLOC(prtos_conf_comm_channel_table, prtos_conf.num_of_comm_channels * sizeof(struct prtos_conf_comm_channel));
    DO_REALLOC(prtos_conf_comm_channel_table_line_number, prtos_conf.num_of_comm_channels * sizeof(struct prtos_conf_comm_channel_line_number));
    memset(&prtos_conf_comm_channel_table[C_COMM_CHANNEL], 0, sizeof(struct prtos_conf_comm_channel));
    memset(&prtos_conf_comm_channel_table_line_number[C_COMM_CHANNEL], 0, sizeof(struct prtos_conf_comm_channel_line_number));
    prtos_conf_comm_channel_table[C_COMM_CHANNEL].s.valid_period = (prtos_u32_t)-1;
}

static void queuing_channel_node_handle0(xmlNodePtr node) {
    prtos_conf.num_of_comm_channels++;
    DO_REALLOC(prtos_conf_comm_channel_table, prtos_conf.num_of_comm_channels * sizeof(struct prtos_conf_comm_channel));
    DO_REALLOC(prtos_conf_comm_channel_table_line_number, prtos_conf.num_of_comm_channels * sizeof(struct prtos_conf_comm_channel_line_number));
    memset(&prtos_conf_comm_channel_table[C_COMM_CHANNEL], 0, sizeof(struct prtos_conf_comm_channel));
    memset(&prtos_conf_comm_channel_table_line_number[C_COMM_CHANNEL], 0, sizeof(struct prtos_conf_comm_channel_line_number));
}

static void sampling_channel_node_handle1(xmlNodePtr node) {
    prtos_conf_comm_channel_table[C_COMM_CHANNEL].type = PRTOS_SAMPLING_CHANNEL;
    prtos_conf_comm_channel_table_line_number[C_COMM_CHANNEL].type = node->line;
}

static struct node_xml sampling_channel_node = {BAD_CAST "SamplingChannel",
                                                sampling_channel_node_handle0,
                                                sampling_channel_node_handle1,
                                                0,
                                                (struct attr_xml *[]){&max_message_length_samping_channel_attr, &valid_period_sampling_channel_attr, 0},
                                                (struct node_xml *[]){&source_ipc_port_node, &destination_ipc_port_node, 0}};

static void max_message_length_queuing_channel_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_comm_channel_table[C_COMM_CHANNEL].q.max_length = to_size((char *)val);
    prtos_conf_comm_channel_table_line_number[C_COMM_CHANNEL].q.max_length = node->line;
}

static void max_num_of_messages_queuing_channel_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_comm_channel_table[C_COMM_CHANNEL].q.max_num_of_msgs = to_u32((char *)val, 10);
    prtos_conf_comm_channel_table_line_number[C_COMM_CHANNEL].q.max_num_of_msgs = node->line;
}

static struct attr_xml max_message_length_queuing_channel_attr = {BAD_CAST "maxMessageLength", max_message_length_queuing_channel_attr_handle};

static struct attr_xml max_num_of_messages_queuing_channel_attr = {BAD_CAST "maxNoMessages", max_num_of_messages_queuing_channel_attr_handle};

static void queuing_channel_node_handle1(xmlNodePtr node) {
    prtos_conf_comm_channel_table[C_COMM_CHANNEL].type = PRTOS_QUEUING_CHANNEL;
    prtos_conf_comm_channel_table_line_number[C_COMM_CHANNEL].type = node->line;
}

static struct node_xml queuing_channel_node = {BAD_CAST "QueuingChannel",
                                               queuing_channel_node_handle0,
                                               queuing_channel_node_handle1,
                                               0,
                                               (struct attr_xml *[]){&max_message_length_queuing_channel_attr, &max_num_of_messages_queuing_channel_attr, 0},
                                               (struct node_xml *[]){&source_ipc_port_node, &destination_ipc_port_node, 0}};

static prtos_s32_t ipvi_id;
struct src_ipvi *src_ipvi;
struct dst_ipvi *dst_ipvi;
struct src_ipvi_line_number *src_ipvi_line_number;
struct dst_ipvi_line_number *dst_ipvi_line_number;

static void src_id_ipvi_attr_handle(xmlNodePtr node, const xmlChar *val) {
    int id = to_u32((char *)val, 10), e;
    for (e = 0; e < num_of_src_ipvi; e++) {
        src_ipvi = &src_ipvi_table[e];
        src_ipvi_line_number = &src_ipvi_table_line_number[e];
        // if (src_ipvi->id==id)
        //   break;
    }
    if (e >= num_of_src_ipvi) {
        num_of_src_ipvi++;
        DO_REALLOC(src_ipvi_table, num_of_src_ipvi * sizeof(struct src_ipvi));
        DO_REALLOC(src_ipvi_table_line_number, num_of_src_ipvi * sizeof(struct src_ipvi_line_number));
        src_ipvi = &src_ipvi_table[num_of_src_ipvi - 1];
        src_ipvi_line_number = &src_ipvi_table_line_number[num_of_src_ipvi - 1];
        memset(src_ipvi, 0, sizeof(struct src_ipvi));
        memset(src_ipvi_line_number, 0, sizeof(struct src_ipvi_line_number));
        src_ipvi->id = id;
        src_ipvi_line_number->id = node->line;
    }
    src_ipvi->ipvi_id = ipvi_id;
}

static struct attr_xml src_id_ipvi_attr = {BAD_CAST "sourceId", src_id_ipvi_attr_handle};

static void ipvi_id_call_back_func(int line, char *val) {
    int id = to_u32((char *)val, 10), e;
    printf("src_ipvi->num_of_dsts: %d\n", src_ipvi->num_of_dsts);
    for (e = 0; e < src_ipvi->num_of_dsts; e++) {
        dst_ipvi = &src_ipvi->dst[e];
        dst_ipvi_line_number = &src_ipvi_line_number->dst[e];
        if (dst_ipvi->id == id) break;
    }

    if (e >= src_ipvi->num_of_dsts) {
        src_ipvi->num_of_dsts++;
        DO_REALLOC(src_ipvi->dst, src_ipvi->num_of_dsts * sizeof(struct dst_ipvi));
        DO_REALLOC(src_ipvi_line_number->dst, src_ipvi->num_of_dsts * sizeof(struct dst_ipvi_line_number));
        dst_ipvi = &src_ipvi->dst[src_ipvi->num_of_dsts - 1];
        dst_ipvi_line_number = &src_ipvi_line_number->dst[src_ipvi->num_of_dsts - 1];
        memset(dst_ipvi, 0, sizeof(struct dst_ipvi));
        memset(dst_ipvi_line_number, 0, sizeof(struct dst_ipvi_line_number));
        dst_ipvi->id = id;
        dst_ipvi_line_number->id = line;
    } else {
        line_error(line, "Duplicated ipvi partition destination");
    }
}

static void dst_id_ipvi_attr_handle(xmlNodePtr node, const xmlChar *val) {
    process_id_list((char *)val, ipvi_id_call_back_func, node->line);
}

static struct attr_xml dst_id_ipvi_attr = {BAD_CAST "destinationId", dst_id_ipvi_attr_handle};

static void id_ipvi_attr_handle(xmlNodePtr node, const xmlChar *val) {
    ipvi_id = to_u32((char *)val, 10);
    if ((ipvi_id < 0) || (ipvi_id > CONFIG_PRTOS_MAX_IPVI)) line_error(node->line, "Invalid ipvi id %d", ipvi_id);
}

static struct attr_xml id_ipvi_attr = {BAD_CAST "id", id_ipvi_attr_handle};

static struct node_xml ipvi_channel_node = {BAD_CAST "Ipvi",         0, 0, 0, (struct attr_xml *[]){&id_ipvi_attr, &src_id_ipvi_attr, &dst_id_ipvi_attr, 0},
                                            (struct node_xml *[]){0}};

static struct node_xml channels_node = {
    BAD_CAST "Channels", 0, 0, 0, 0, (struct node_xml *[]){&sampling_channel_node, &queuing_channel_node, &ipvi_channel_node, 0}};

static void int_lines_attr_handle(xmlNodePtr node, const xmlChar *val) {
    to_hw_irq_lines((char *)val, node->line);
}

static struct attr_xml int_lines_attr = {BAD_CAST "lines", int_lines_attr_handle};

static struct node_xml hw_interrupts_node = {BAD_CAST "Interrupts", 0, 0, 0, (struct attr_xml *[]){&int_lines_attr, 0}, 0};

extern struct node_xml io_ports_node;

static struct node_xml hw_resources_partition_node = {BAD_CAST "HwResources", 0, 0, 0, 0, (struct node_xml *[]){&io_ports_node, &hw_interrupts_node, 0}};

static void name_comm_port_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_comm_port_table[C_COMMPORT].name_offset = add_string((char *)val);
    prtos_conf_comm_port_table_line_number[C_COMMPORT].name = node->line;
}

static struct attr_xml name_comm_port_attr = {BAD_CAST "name", name_comm_port_attr_handle};

static void direction_comm_port_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_comm_port_table[C_COMMPORT].direction = to_comm_port_direction((char *)val, node->line);
    prtos_conf_comm_port_table_line_number[C_COMMPORT].direction = node->line;
}

static struct attr_xml direction_comm_port_attr = {BAD_CAST "direction", direction_comm_port_attr_handle};

static void type_comm_port_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_comm_port_table[C_COMMPORT].type = to_comm_port_type((char *)val, node->line);
    prtos_conf_comm_port_table_line_number[C_COMMPORT].type = node->line;
}

static struct attr_xml type_comm_port_attr = {BAD_CAST "type", type_comm_port_attr_handle};

static void comm_port_node_handle0(xmlNodePtr node) {
    prtos_conf.num_of_comm_ports++;
    prtos_conf_partition_table[C_PARTITION].num_of_ports++;
    DO_REALLOC(prtos_conf_comm_port_table, prtos_conf.num_of_comm_ports * sizeof(struct prtos_conf_comm_port));
    DO_REALLOC(prtos_conf_comm_port_table_line_number, prtos_conf.num_of_comm_ports * sizeof(struct prtos_conf_comm_port_code_line_number));
    memset(&prtos_conf_comm_port_table[C_COMMPORT], 0, sizeof(struct prtos_conf_comm_port));
    memset(&prtos_conf_comm_port_table_line_number[C_COMMPORT], 0, sizeof(struct prtos_conf_comm_port_code_line_number));
    prtos_conf_comm_port_table[C_COMMPORT].channel_id = -1;
}

static void comm_port_node_handle1(xmlNodePtr node) {
    check_port_name(C_COMMPORT, C_PARTITION);
}

static struct node_xml comm_port_node = {BAD_CAST "Port",
                                         comm_port_node_handle0,
                                         comm_port_node_handle1,
                                         0,
                                         (struct attr_xml *[]){&name_comm_port_attr, &direction_comm_port_attr, &type_comm_port_attr, 0},
                                         0};

static void port_table_node_handle0(xmlNodePtr node) {
    prtos_conf_partition_table[C_PARTITION].comm_ports_offset = prtos_conf.num_of_comm_ports;
}

static struct node_xml port_table_node = {BAD_CAST "PortTable", port_table_node_handle0, 0, 0, 0, (struct node_xml *[]){&comm_port_node, 0}};

static void id_partition_attr_handle(xmlNodePtr node, const xmlChar *val) {
    int d = to_u32((char *)val, 10);

    if (d != C_PARTITION) line_error(node->line, "partition id (%d) shall be consecutive starting at 0", d);

    prtos_conf_partition_table[C_PARTITION].id = d;
    prtos_conf_partition_table_line_number[C_PARTITION].id = node->line;
}

static struct attr_xml id_partition_attr = {BAD_CAST "id", id_partition_attr_handle};

static void name_partition_attr_handle(xmlNodePtr node, const xmlChar *val) {
    check_partition_name((char *)val, node->line);
    prtos_conf_partition_table[C_PARTITION].name_offset = add_string((char *)val);
    prtos_conf_partition_table_line_number[C_PARTITION].name = node->line;
}

static struct attr_xml name_partition_attr = {BAD_CAST "name", name_partition_attr_handle};

static void console_partition_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_partition_table[C_PARTITION].console_device.id = add_dev_name((char *)val);
    prtos_conf_partition_table_line_number[C_PARTITION].console_device = node->line;
}

static struct attr_xml console_partition_attr = {BAD_CAST "console", console_partition_attr_handle};

static void num_of_vcpus_partition_attr_handle(xmlNodePtr node, const xmlChar *val) {
    int d = to_u32((char *)val, 10);
    prtos_conf_partition_table[C_PARTITION].num_of_vcpus = d;
    prtos_conf_partition_table_line_number[C_PARTITION].num_of_vcpus = node->line;
    if (prtos_conf_partition_table[C_PARTITION].num_of_vcpus > CONFIG_MAX_NO_VCPUS)
        line_error(node->line, "too many virtual Cpus (%d) allocated to the partition", d);
}

static struct attr_xml num_of_vcpus_partition_attr = {BAD_CAST "noVCpus", num_of_vcpus_partition_attr_handle};

static void flags_partition_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf_partition_table[C_PARTITION].flags = to_partition_flags((char *)val, node->line);
    prtos_conf_partition_table_line_number[C_PARTITION].flags = node->line;
}

static struct attr_xml flags_partition_attr = {BAD_CAST "flags", flags_partition_attr_handle};

static void partition_node_handle0(xmlNodePtr node) {
    prtos_conf.num_of_partitions++;
    count_partition = C_PARTITION;
    DO_REALLOC(prtos_conf_partition_table, prtos_conf.num_of_partitions * sizeof(struct prtos_conf_part));
    DO_REALLOC(prtos_conf_partition_table_line_number, prtos_conf.num_of_partitions * sizeof(struct prtos_conf_part_line_number));
    memset(&prtos_conf_partition_table[C_PARTITION], 0, sizeof(struct prtos_conf_part));
    memset(&prtos_conf_partition_table_line_number[C_PARTITION], 0, sizeof(struct prtos_conf_part_line_number));
    prtos_conf_partition_table[C_PARTITION].console_device.id = PRTOS_DEV_INVALID_ID;
    prtos_conf_partition_table[C_PARTITION].physical_memory_areas_offset = prtos_conf.num_of_physical_memory_areas;
    num_of_mem_area_ptr = &prtos_conf_partition_table[C_PARTITION].num_of_physical_memory_areas;
    hm_table_ptr = prtos_conf_partition_table[C_PARTITION].hm_table;
    trace_ptr = &prtos_conf_partition_table[C_PARTITION].trace;
    trace_line_number_ptr = &prtos_conf_partition_table_line_number[C_PARTITION].trace;
    trace_ptr->dev.id = PRTOS_DEV_INVALID_ID;
    setup_default_part_hm_actions(prtos_conf_partition_table[C_PARTITION].hm_table);
}

static void partition_node_handle2(xmlNodePtr node) {
    check_mem_area_per_partition();
}

static struct node_xml partition_node = {
    BAD_CAST "Partition",
    partition_node_handle0,
    0,
    partition_node_handle2,
    (struct attr_xml *[]){&id_partition_attr, &name_partition_attr, &console_partition_attr, &flags_partition_attr, &num_of_vcpus_partition_attr, 0},
    (struct node_xml *[]){&mem_area_node, &hm_node, &trace_node, &hw_resources_partition_node, &port_table_node, 0}};

static struct node_xml partition_table_node = {BAD_CAST "PartitionTable", 0, 0, 0, 0, (struct node_xml *[]){&partition_node, 0}};

static void resident_sw_node_handle0(xmlNodePtr node) {
    prtos_conf.rsw.physical_memory_areas_offset = prtos_conf.num_of_physical_memory_areas;
    num_of_mem_area_ptr = &prtos_conf.rsw.num_of_physical_memory_areas;
    count_partition = -1;
}

static struct node_xml resident_sw_node = {BAD_CAST "ResidentSw", resident_sw_node_handle0, 0, 0, 0, (struct node_xml *[]){&mem_area_node, 0}};

static void console_handle_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf.hpv.console_device.id = add_dev_name((char *)val);
    prtos_conf_line_number.hpv.console_device = node->line;
}

static struct attr_xml console_handle_attr = {BAD_CAST "console", console_handle_attr_handle};

static void hm_device_handle_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf.hpv.hm_device.id = add_dev_name((char *)val);
    prtos_conf_line_number.hpv.hm_device = node->line;
}

static struct attr_xml hm_dev_handle_attr = {BAD_CAST "healthMonitorDevice", hm_device_handle_attr_handle};

static void prtos_hypervisor_node_handle0(xmlNodePtr node) {
    prtos_conf.hpv.physical_memory_areas_offset = prtos_conf.num_of_physical_memory_areas;
    num_of_mem_area_ptr = &prtos_conf.hpv.num_of_physical_memory_areas;
    setup_default_hyp_hm_actions(prtos_conf.hpv.hm_table);
    hm_table_ptr = prtos_conf.hpv.hm_table;
    trace_ptr = &prtos_conf.hpv.trace;
    trace_line_number_ptr = &prtos_conf_line_number.hpv.trace;
    trace_ptr->dev.id = PRTOS_DEV_INVALID_ID;
    count_partition = -2;
}

static void prtos_hypervisor_node_handle2(xmlNodePtr node) {
    check_hpv_mem_area_flags();
}
static struct node_xml prtos_hypervisor_node = {BAD_CAST "PRTOSHypervisor",
                                                prtos_hypervisor_node_handle0,
                                                0,
                                                prtos_hypervisor_node_handle2,
                                                (struct attr_xml *[]){&console_handle_attr, &hm_dev_handle_attr, 0},
                                                (struct node_xml *[]){&hyp_mem_area_node, &hm_node, &trace_hyp_node, 0}};

static void hw_description_node_handle2(xmlNodePtr node) {
    // CheckMemBlockReg();
}

static struct node_xml hw_description_node = {
    BAD_CAST "HwDescription", 0, 0, hw_description_node_handle2, 0, (struct node_xml *[]){&processor_table_node, &memory_layout_node, &devices_node, 0}};

static void version_sys_desc_attr_handle(xmlNodePtr node, const xmlChar *val) {
    unsigned int version, subversion, revision;

    sscanf((char *)val, "%u.%u.%u", &version, &subversion, &revision);
    prtos_conf.file_version = PRTOSC_SET_VERSION(version, subversion, revision);
    prtos_conf_line_number.file_version = node->line;
}

static struct attr_xml version_sys_desc_attr = {BAD_CAST "version", version_sys_desc_attr_handle};

static void name_sys_desc_attr_handle(xmlNodePtr node, const xmlChar *val) {
    prtos_conf.name_offset = add_string((char *)val);
}

static struct attr_xml name_sys_desc_attr = {BAD_CAST "name", name_sys_desc_attr_handle};

static void system_description_node_handle0(xmlNodePtr node) {
    int e;
    memset(&prtos_conf, 0, sizeof(struct prtos_conf));
    memset(&prtos_conf_line_number, 0, sizeof(struct prtos_conf_line_number));
    prtos_conf.hpv.console_device.id = PRTOS_DEV_INVALID_ID;
    prtos_conf.hpv.hm_device.id = PRTOS_DEV_INVALID_ID;
    prtos_conf.hpv.trace.dev.id = PRTOS_DEV_INVALID_ID;
    for (e = 0; e < CONFIG_NO_HWIRQS; e++) prtos_conf.hpv.hw_irq_table[e].owner = -1;
}

static void system_description_node_handle2(xmlNodePtr node) {
    check_max_num_of_kthreads();
    check_cyclic_plan_partition_id();
    check_cyclic_plan_vcpuid();

    check_part_not_alloc_to_more_than_a_cpu();

    check_io_ports();
    link_devices();
    link_channels_to_ports();
    check_ipvi_table();
    process_ipvi_table();
    setup_hw_irq_mask();
    hm_check_exist_maintenance_plan();
    sort_phys_mem_areas();
    sort_mem_regions();
}
static struct node_xml system_description_node = {
    BAD_CAST "SystemDescription",
    system_description_node_handle0,
    0,
    system_description_node_handle2,
    (struct attr_xml *[]){&version_sys_desc_attr, &name_sys_desc_attr, 0},
    (struct node_xml *[]){&hw_description_node, &prtos_hypervisor_node, &resident_sw_node, &partition_table_node, &channels_node, 0}};

struct node_xml *root_handlers[] = {&system_description_node, 0};

// static struct attr_xml _attr={BAD_CAST"", 0};
// static struct node_xml _node={BAD_CAST"", 0, 0, (struct attr_xml *[]){0}, (struct node_xml *[]){0}};
