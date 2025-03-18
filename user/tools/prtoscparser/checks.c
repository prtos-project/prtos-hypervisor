/*
 * FILE: checks.c
 *
 * checks implementation
 *
 * www.prtos.org
 */

#define _RSV_PHYS_PAGES_
#define _RSV_HW_IRQS_

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <prtos_inc/arch/ginfo.h>
#include <prtos_inc/arch/paging.h>

#include "limits.h"
#include "common.h"
#include "parser.h"
#include "prtos_conf.h"

void check_all_memreg(void) {
    prtos_address_t a, b;
    int e, line;

    a = prtos_conf_mem_reg_table[0].start_addr;
    line = prtos_conf_mem_reg_table_line_number[0].start_addr;
    for (e = 1; e < prtos_conf.num_of_regions; e++) {
        b = prtos_conf_mem_reg_table[e].start_addr;
        if (a >= b) line_error(line, "memory region (0x%x) unsorted", a);
        a = b;
        line = prtos_conf_mem_reg_table_line_number[e].start_addr;
    }
}

void check_all_mem_areas(struct prtos_conf_memory_area *mem_areas, struct prtos_conf_memory_area_line_number *mem_area_line_number, int len) {
    prtos_address_t a, b;
    int e, line;

    a = mem_areas[0].start_addr;
    line = mem_area_line_number[0].start_addr;
    for (e = 1; e < len; e++) {
        b = mem_areas[e].start_addr;
        if (a >= b) line_error(line, "memory area (0x%x) unsorted", a);
        a = b;
        line = mem_area_line_number[e].start_addr;
    }
}

void check_memory_region(int region) {
    prtos_address_t s0, e0, s1, e1;
    int e;
    s0 = prtos_conf_mem_reg_table[region].start_addr;
    e0 = s0 + prtos_conf_mem_reg_table[region].size - 1;
    if (s0 & (PAGE_SIZE - 1))
        line_error(prtos_conf_mem_reg_table_line_number[region].start_addr, "memory region start address (0x%x) shall be aligned to 0x%x", s0, PAGE_SIZE);
    if (prtos_conf_mem_reg_table[region].size & (PAGE_SIZE - 1))
        line_error(prtos_conf_mem_reg_table_line_number[region].size, "memory region size (%d) is not multiple of %d", prtos_conf_mem_reg_table[region].size,
                   PAGE_SIZE);
    for (e = 0; e < prtos_conf.num_of_regions - 1; e++) {
        s1 = prtos_conf_mem_reg_table[e].start_addr;
        e1 = s1 + prtos_conf_mem_reg_table[e].size - 1;

        if ((s0 >= e1) || (s1 >= e0)) continue;
        line_error(prtos_conf_mem_reg_table_line_number[region].line, "memory region [0x%x - 0x%x] overlaps [0x%x - 0x%x] (line %d)", s0, e0, s1, e1,
                   prtos_conf_mem_reg_table_line_number[e].line);
    }
}

int check_phys_mem_area(int mem_area) {
    extern prtos_s32_t num_of_rsv_phys_pages;
    prtos_address_t s0, e0, s1, e1;
    int e, found;

    s0 = prtos_conf_mem_area_table[mem_area].start_addr;
    e0 = s0 + prtos_conf_mem_area_table[mem_area].size - 1;

    if (s0 & (PAGE_SIZE - 1))
        line_error(prtos_conf_mem_area_table_line_number[mem_area].start_addr, "memory area start address (0x%x) shall be aligned to 0x%x", s0, PAGE_SIZE);
    if (prtos_conf_mem_area_table[mem_area].size & (PAGE_SIZE - 1))
        line_error(prtos_conf_mem_area_table[mem_area].size, "memory area size (%d) is not multiple of %d", prtos_conf_mem_area_table[mem_area].size,
                   PAGE_SIZE);
#if !defined(CONFIG_AARCH64)  // FIXME: this is just a WA for arm64 build
    for (e = 0; e < num_of_rsv_phys_pages; e++) {
        s1 = rsv_phys_pages[e].address;
        e1 = s1 + (rsv_phys_pages[e].num_of_pages * PAGE_SIZE) - 1;
        if (!((e1 < s0) || (s1 >= e0)))
            line_error(prtos_conf_mem_area_table_line_number[mem_area].line,
                       "memory area [0x%x - 0x%x] overlaps a memory area [0x%x - 0x%x] reserved for PRTOS", s0, e0, s1, e1);
    }
#endif
    for (e = 0, found = -1; e < prtos_conf.num_of_regions; e++) {
        s1 = prtos_conf_mem_reg_table[e].start_addr;
        e1 = s1 + prtos_conf_mem_reg_table[e].size - 1;
        if ((s0 >= s1) && (e0 <= e1)) found = e;
    }
    if (found < 0) line_error(prtos_conf_mem_area_table_line_number[mem_area].line, "memory area [0x%x - 0x%x] is not covered by any memory region", s0, e0);

    for (e = 0; e < prtos_conf.num_of_physical_memory_areas - 1; e++) {
        s1 = prtos_conf_mem_area_table[e].start_addr;
        e1 = s1 + prtos_conf_mem_area_table[e].size - 1;
        if ((s0 >= e1) || (s1 >= e0)) continue;

        if ((prtos_conf_mem_area_table[e].flags & prtos_conf_mem_area_table[mem_area].flags & PRTOS_MEM_AREA_SHARED) == PRTOS_MEM_AREA_SHARED) continue;
        line_error(prtos_conf_mem_area_table_line_number[mem_area].line, "memory area [0x%x - 0x%x] overlaps [0x%x - 0x%x] (line %d)", s0, e0, s1, e1,
                   prtos_conf_mem_area_table_line_number[e].line);
    }

    return found;
}

void check_mem_area_per_partition(void) {
    prtos_address_t s0, e0, s1, e1;
    int i, j, offset;

    for (i = 0; i < prtos_conf_partition_table[C_PARTITION].num_of_physical_memory_areas - 1; i++) {
        offset = prtos_conf_partition_table[C_PARTITION].physical_memory_areas_offset;
        s0 = prtos_conf_mem_area_table[i + offset].mapped_at;
        e0 = s0 + prtos_conf_mem_area_table[i + offset].size - 1;
        for (j = i + 1; j < prtos_conf_partition_table[C_PARTITION].num_of_physical_memory_areas; j++) {
            s1 = prtos_conf_mem_area_table[j + offset].mapped_at;
            e1 = s1 + prtos_conf_mem_area_table[j + offset].size - 1;
            if ((s0 >= e1) || (s1 >= e0)) continue;
            line_error(prtos_conf_mem_area_table_line_number[i + offset].line, "virtual memory area [0x%x - 0x%x] overlaps [0x%x - 0x%x] (line %d)", s0, e0, s1,
                       e1, prtos_conf_mem_area_table_line_number[j + offset].line);
        }
    }
}

void check_hw_irq(int line, int line_number) {
    int e;
    for (e = 0; e < num_of_rsv_hw_irqs; e++)
        if (line == rsv_hw_irqs[e]) line_error(line_number, "hw interrupt line %d reserved for PRTOS", rsv_hw_irqs[e]);
}

void check_port_name(int port, int partition) {
    int e, offset;
    offset = prtos_conf_partition_table[partition].comm_ports_offset;
    for (e = 0; e < prtos_conf_partition_table[partition].num_of_ports - 1; e++)
        if (!strcmp(&str_tables[prtos_conf_comm_port_table[e + offset].name_offset], &str_tables[prtos_conf_comm_port_table[port].name_offset]))
            line_error(prtos_conf_comm_port_table_line_number[port].name, "port name \"%s\" duplicated (line %d)",
                       &str_tables[prtos_conf_comm_port_table[port].name_offset], prtos_conf_comm_port_table_line_number[e + offset].name);
}

void check_hpv_mem_area_flags(void) {
    prtos_u32_t flags;
    int e, line;

    for (e = 0; e < prtos_conf.hpv.num_of_physical_memory_areas; e++) {
        flags = prtos_conf_mem_area_table[e + prtos_conf.hpv.physical_memory_areas_offset].flags;
        line = prtos_conf_mem_area_table_line_number[e + prtos_conf.hpv.physical_memory_areas_offset].flags;
        if (flags & PRTOS_MEM_AREA_SHARED) line_error(line, "\"shared\" flag not permitted");

        if (flags & PRTOS_MEM_AREA_READONLY) line_error(line, "\"read-only\" flag not permitted");

        if (flags & PRTOS_MEM_AREA_UNMAPPED) line_error(line, "\"unmaped\" flag not permitted");
    }
}

#if defined(CONFIG_DEV_UART) || defined(CONFIG_DEV_UART_MODULE)
void check_uart_id(int uart_id, int line) {
    if ((uart_id < 0) || (uart_id >= CONFIG_DEV_NO_UARTS)) line_error(line, "invalid uart id %d", uart_id);
}
#endif

void check_sched_cyclic_plan(struct prtos_conf_sched_cyclic_plan *plan, struct prtos_conf_sched_cyclic_plan_line_number *plan_line_number) {
    prtos_u32_t t;
    int e;

    for (t = 0, e = 0; e < plan->num_of_slots; e++) {
        if (t > prtos_conf_sched_cyclic_slot_table[plan->slots_offset + e].start_exec)
            line_error(prtos_conf_sched_cyclic_slot_table_line_number[plan->slots_offset + e].start_exec,
                       "slot %d ([%lu - %lu] usec) overlaps slot %d ([%lu - %lu] usec)", e,
                       prtos_conf_sched_cyclic_slot_table[plan->slots_offset + e].start_exec,
                       prtos_conf_sched_cyclic_slot_table[plan->slots_offset + e].end_exec, e - 1,
                       prtos_conf_sched_cyclic_slot_table[plan->slots_offset + e - 1].start_exec,
                       prtos_conf_sched_cyclic_slot_table[plan->slots_offset + e - 1].end_exec);
        if ((t = prtos_conf_sched_cyclic_slot_table[plan->slots_offset + e].end_exec) > plan->major_frame)
            line_error(prtos_conf_sched_cyclic_slot_table_line_number[plan->slots_offset + e].end_exec,
                       "last slot ([%lu - %lu] usec) overlaps major frame (%lu usec)", prtos_conf_sched_cyclic_slot_table[plan->slots_offset + e].start_exec,
                       prtos_conf_sched_cyclic_slot_table[plan->slots_offset + e].end_exec, plan->major_frame);
    }
}

void check_cyclic_plan_partition_id(void) {
    struct prtos_conf_sched_cyclic_plan *plan;
    int j, k;

    for (j = 0; j < prtos_conf.num_of_sched_cyclic_plans; j++) {
        plan = &prtos_conf_sched_cyclic_plan_table[j];
        for (k = 0; k < plan->num_of_slots; k++) {
            if ((prtos_conf_sched_cyclic_slot_table[k + plan->slots_offset].partition_id < 0) ||
                (prtos_conf_sched_cyclic_slot_table[k + plan->slots_offset].partition_id >= prtos_conf.num_of_partitions))
                line_error(prtos_conf_sched_cyclic_slot_table_line_number[k + plan->slots_offset].partition_id, "incorrect partition id (%d)",
                           prtos_conf_sched_cyclic_slot_table[k + plan->slots_offset].partition_id);
        }
    }
}

void check_part_not_alloc_to_more_than_a_cpu(void) {
    struct prtos_conf_sched_cyclic_plan *plan;
    struct prtos_conf_sched_cyclic_slot *slot;
    int i, j;
    int e;

    vcpu_to_cpu_table = malloc(prtos_conf.num_of_partitions * sizeof(struct vcpu_to_cpu *));
    for (e = 0; e < prtos_conf.num_of_partitions; e++) {
        vcpu_to_cpu_table[e] = malloc(prtos_conf_partition_table[e].num_of_vcpus * sizeof(struct vcpu_to_cpu));
        memset(vcpu_to_cpu_table[e], -1, prtos_conf_partition_table[e].num_of_vcpus * sizeof(struct vcpu_to_cpu));
    }

    for (e = 0; e < prtos_conf.hpv.num_of_cpus; e++) {
        for (i = 0; i < prtos_conf.hpv.cpu_table[e].num_of_sched_cyclic_plans; i++) {
            plan = &prtos_conf_sched_cyclic_plan_table[i + prtos_conf.hpv.cpu_table[e].sched_cyclic_plans_offset];
            for (j = 0; j < plan->num_of_slots; j++) {
                slot = &prtos_conf_sched_cyclic_slot_table[j + plan->slots_offset];
                if (vcpu_to_cpu_table[slot->partition_id][slot->vcpu_id].cpu == -1) {
                    vcpu_to_cpu_table[slot->partition_id][slot->vcpu_id].cpu = e;
                    vcpu_to_cpu_table[slot->partition_id][slot->vcpu_id].line = prtos_conf_sched_cyclic_slot_table_line_number[j + plan->slots_offset].vCpuId;
                    continue;
                }
                if (vcpu_to_cpu_table[slot->partition_id][slot->vcpu_id].cpu != e)
                    line_error(prtos_conf_sched_cyclic_slot_table_line_number[j + plan->slots_offset].vCpuId,
                               "vCpu (%d, %d) assigned to CPU %d and %d (line %d)", slot->partition_id, slot->vcpu_id,
                               vcpu_to_cpu_table[slot->partition_id][slot->vcpu_id].cpu, e, vcpu_to_cpu_table[slot->partition_id][slot->vcpu_id].line);
            }
        }
    }
}

void check_cyclic_plan_vcpuid(void) {
    struct prtos_conf_sched_cyclic_plan *plan;
    int j, k;

    for (j = 0; j < prtos_conf.num_of_sched_cyclic_plans; j++) {
        plan = &prtos_conf_sched_cyclic_plan_table[j];
        for (k = 0; k < plan->num_of_slots; k++) {
            if ((prtos_conf_sched_cyclic_slot_table[k + plan->slots_offset].vcpu_id < 0) ||
                (prtos_conf_sched_cyclic_slot_table[k + plan->slots_offset].vcpu_id >=
                 prtos_conf_partition_table[prtos_conf_sched_cyclic_slot_table[k + plan->slots_offset].partition_id].num_of_vcpus))
                line_error(prtos_conf_sched_cyclic_slot_table_line_number[k + plan->slots_offset].vCpuId, "incorrect vCpu id (%d)",
                           prtos_conf_sched_cyclic_slot_table[k + plan->slots_offset].vcpu_id);
        }
    }
}

void check_partition_name(char *name, int line) {
    int e;
    for (e = 0; e < prtos_conf.num_of_partitions; e++)
        if (!strcmp(&str_tables[prtos_conf_partition_table[e].name_offset], name))
            line_error(line, "partition name \"%s\" duplicated (line %d)", name, prtos_conf_partition_table_line_number[e].name);
}

void check_max_num_of_kthreads(void) {
    int e, num_of_kthreads = prtos_conf.num_of_partitions;
    for (e = 0; e < prtos_conf.num_of_partitions; e++) num_of_kthreads += prtos_conf_partition_table[e].num_of_vcpus;

    if (num_of_kthreads > CONFIG_MAX_NO_KTHREADS)
        error_printf("prtos only supports a maximum of %d kthreads, the configuration file defines a total of %d kthreads", CONFIG_MAX_NO_KTHREADS,
                     num_of_kthreads);
}

void check_ipvi_table(void) {
    struct src_ipvi *src;
    struct dst_ipvi *dst;
    struct src_ipvi_line_number *src_line_number;
    struct dst_ipvi_line_number *dst_line_number;
    int i, j;

    for (i = 0; i < num_of_src_ipvi; i++) {
        src = &src_ipvi_table[i];
        src_line_number = &src_ipvi_table_line_number[i];
        if ((src->id < 0) || (src->id >= prtos_conf.num_of_partitions)) line_error(src_line_number->id, "source partition %d doesn't exist", src->id);
        for (j = 0; j < src->num_of_dsts; j++) {
            dst = &src->dst[j];
            dst_line_number = &src_line_number->dst[j];
            if ((dst->id < 0) || (dst->id >= prtos_conf.num_of_partitions)) line_error(dst_line_number->id, "destination partition %d doesn't exist", dst->id);
        }
    }
}
