/*
 * FILE: riscv64.c
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
#include <prtos_inc/arch/ginfo.h>

#include "parser.h"
#include "conv.h"
#include "common.h"
#include "prtos_conf.h"
#include <prtos_inc/arch/asm_offsets.h>

#define _PHYS2VIRT(x) ((prtos_u64_t)(x) + CONFIG_PRTOS_OFFSET - CONFIG_PRTOS_LOAD_ADDR)

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

/* RISC-V has no IO ports; provide stubs */
static void io_ports_node_handle0(xmlNodePtr node) {
    line_error(node->line, "IoPorts not supported on RISC-V");
}

struct node_xml io_ports_node = {BAD_CAST "IoPorts", io_ports_node_handle0, 0, 0, 0, 0};

void generate_io_port_table(FILE *out_file) {
    /* RISC-V has no IO ports, generate empty table */
    fprintf(out_file, "const struct prtos_conf_io_port prtos_conf_io_port_table[] = {};\n\n");
}

void check_io_ports(void) {}

void arch_mmu_rsv_mem(FILE *out_file) {
    /* No MMU reserved memory needed for riscv64 identity-mapped port */
}

void arch_loader_rsv_mem(FILE *out_file) {
    /* No loader reserved memory needed for riscv64 */
}
