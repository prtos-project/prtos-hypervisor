/*
 * FILE: x86.c
 *
 * architecture dependent stuff
 *
 * www.prtos.org
 */

#define _RSV_IO_PORTS_
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <prtos_inc/arch/paging.h>
#include <prtos_inc/guest.h>
#include <prtos_inc/arch/segments.h>
#include <prtos_inc/arch/ginfo.h>

#include "parser.h"
#include "conv.h"
#include "common.h"
#include "prtos_conf.h"

#define _PHYS2VIRT(x) ((prtos_u32_t)(x) + CONFIG_PRTOS_OFFSET - CONFIG_PRTOS_LOAD_ADDR)

prtos_u32_t to_region_flags(char *s) {
    prtos_u32_t flags = 0;
    if (!strcasecmp(s, "ram")) {
        flags = PRTOSC_REG_FLAG_PGTAB;
    } else if (!strcasecmp(s, "rom")) {
        flags = PRTOSC_REG_FLAG_ROM;
    } else {
        error_printf("Expected valid region type (%s)\n", s);
    }
    return flags;
}

static prtos_u32_t io_port_base, num_of_io_ports;

static void base_range_attr_handle(xmlNodePtr node, const xmlChar *val) {
    io_port_base = to_u32((char *)val, 16);
}

static struct attr_xml base_range_attr = {BAD_CAST "base", base_range_attr_handle};

static void num_of_ports_range_attr_handle(xmlNodePtr node, const xmlChar *val) {
    num_of_io_ports = to_u32((char *)val, 10);
    prtos_conf_partition_table[C_PARTITION].num_of_io_ports += num_of_io_ports;
}

static struct attr_xml num_of_ports_range_attr = {BAD_CAST "noPorts", num_of_ports_range_attr_handle};

static void io_port_range_node_handle1(xmlNodePtr node) {
    prtos_u32_t port, a, b, c, d;
    prtos_s32_t e;

    c = io_port_base;
    d = c + num_of_io_ports - 1;
    for (e = 0; e < no_of_rsv_io_ports; e++) {
        a = rsv_io_ports[e].base;
        b = a + rsv_io_ports[e].offset - 1;
        if (!((d < a) || (c >= b)))
            line_error(node->line, "io-ports [0x%lx:%d] reserved for prtos ([0x%lx:%d])", io_port_base, num_of_io_ports, rsv_io_ports[e].base,
                       rsv_io_ports[e].offset);
    }

    for (port = c; port < d; port++) prtos_conf_io_port_table[C_IOPORT].map[port / 32] &= ~(1 << (port % 32));
    io_port_base = 0;
    num_of_io_ports = 0;
}

static struct node_xml range_io_port_node = {
    BAD_CAST "Range", 0, io_port_range_node_handle1, 0, (struct attr_xml *[]){&base_range_attr, &num_of_ports_range_attr, 0}, 0};

static void io_port_restricted_node_handle0(xmlNodePtr node) {
    line_error(node->line, "Restricted IoPorts not supported");
}

static struct node_xml restricted_io_port_node = {BAD_CAST "Restricted", io_port_restricted_node_handle0, 0, 0, (struct attr_xml *[]){0}, 0};

static void io_ports_node_handle0(xmlNodePtr node) {
    prtos_s32_t e;
    prtos_conf_partition_table[C_PARTITION].io_ports_offset = prtos_conf.num_of_io_ports;
    prtos_conf.num_of_io_ports++;
    DO_REALLOC(prtos_conf_io_port_table, prtos_conf.num_of_io_ports * sizeof(struct prtos_conf_io_port));
    memset(&prtos_conf_io_port_table[C_IOPORT], 0, sizeof(struct prtos_conf_io_port));
    for (e = 0; e < 2048; e++) prtos_conf_io_port_table[C_IOPORT].map[e] = ~0;
    // prtos_conf_io_port_table[C_IOPORT].map[2047]=0xff000000;
}

struct node_xml io_ports_node = {BAD_CAST "IoPorts", io_ports_node_handle0, 0, 0, 0, (struct node_xml *[]){&range_io_port_node, &restricted_io_port_node, 0}};

void generate_io_port_table(FILE *out_file) {
    int i, j, f;
    fprintf(out_file, "const struct prtos_conf_io_port prtos_conf_io_port_table[] = {\n");
    for (i = 0; i < prtos_conf.num_of_io_ports; i++) {
        fprintf(out_file, ADDNTAB(1, "[%d] = { .map = {\n"), i);
        prtos_conf_io_port_table[i].map[2047] |= 0xff000000;
        for (f = 0, j = 1; j < 2048; j++) {
            if (prtos_conf_io_port_table[i].map[f] != prtos_conf_io_port_table[i].map[j]) {
                if (f != (j - 1))
                    fprintf(out_file, ADDNTAB(2, "[%d ... %d] = 0x%x,\n"), f, j - 1, prtos_conf_io_port_table[i].map[f]);
                else
                    fprintf(out_file, ADDNTAB(2, "[%d] = 0x%x,\n"), f, prtos_conf_io_port_table[i].map[f]);
                f = j;
            }
        }
        if (f != 2047)
            fprintf(out_file, ADDNTAB(2, "[%d ... 2047] = 0x%x,\n"), f, prtos_conf_io_port_table[i].map[f]);
        else
            fprintf(out_file, ADDNTAB(2, "[2047] = 0x%x,\n"), prtos_conf_io_port_table[i].map[f]);
        fprintf(out_file, ADDNTAB(1, "}, },\n"));
    }
    fprintf(out_file, "};\n\n");
}

void check_io_ports(void) {}

#define ROUNDUP(r, v) ((((~(r)) + 1) & ((v)-1)) + (r))

void arch_mmu_rsv_mem(FILE *out_file) {
    prtos_address_t end;

    end = ROUNDUP(_PHYS2VIRT(prtos_conf_mem_area_table[prtos_conf.hpv.physical_memory_areas_offset].start_addr) +
                      prtos_conf_mem_area_table[prtos_conf.hpv.physical_memory_areas_offset].size,
                  LPAGE_SIZE);
    rsv_block((((PRTOS_VMAPEND - end) + 1) >> PTDL1_SHIFT) * PTDL2SIZE, PTDL2SIZE, "canon-ptdL2");
}

void arch_loader_rsv_mem(FILE *out_file) {
    prtos_s32_t i;
    for (i = 0; i < prtos_conf.num_of_partitions; i++) {
        /*        rsv_block(PTDL2SIZE, PTDL2SIZE, "ptdL2Ldr");*/
        rsv_block(18 * PAGE_SIZE, PAGE_SIZE, "Ldr stack");
    }
}
