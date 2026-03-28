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

#define ROUNDUP(r, v) ((((~(r)) + 1) & ((v)-1)) + (r))

/* Maximum number of 4KB frames in the frame area (256MB / 4KB) */
#define VMM_MAX_FRAMES (256 * 1024 * 1024 / PAGE_SIZE)
/* Maximum number of L3 tables needed = 256MB / 2MB */
#define VMM_L3_POOL_COUNT (256 * 1024 * 1024 / LPAGE_SIZE)

/* Sv39x4 G-stage root table: 2048 entries × 8 bytes = 16KB */
#define GSTAGE_ROOT_SIZE 16384

void arch_mmu_rsv_mem(FILE *out_file) {
    prtos_u64_t frame_start;
    prtos_u64_t num_l2;
    int i, j;

    /* S-stage: Frame area starts at the next 1GB boundary after CONFIG_PRTOS_OFFSET
     * (matching the runtime computation in vmmap.c) */
    frame_start = (CONFIG_PRTOS_OFFSET & ~((prtos_u64_t)(1ULL << PTDL1_SHIFT) - 1)) + (1ULL << PTDL1_SHIFT);

    /* L2 tables for frame area (one per 1GB region from frame_start to VMAPEND) */
    num_l2 = ((PRTOS_VMAPEND - frame_start + 1) >> PTDL1_SHIFT);
    rsv_block((unsigned int)(num_l2 * PTDL2SIZE), PTDL2SIZE, "canon-ptdL2");

    /* S-stage: L3 table pool for vm_map_page fine-grained 4KB mappings */
    rsv_block(VMM_L3_POOL_COUNT * PTDL3SIZE, PTDL3SIZE, "l3 pool");

    /* G-stage: per-partition stage-2 page tables (like aarch64 pattern) */
    for (i = 0; i < prtos_conf.num_of_partitions; i++) {
        /* Root table: 16KB, 16KB-aligned (Sv39x4, 2048 entries) */
        rsv_block(GSTAGE_ROOT_SIZE, GSTAGE_ROOT_SIZE, "s2 root");
        /* L1 tables: 4KB each, up to 2 per partition */
        rsv_block(PAGE_SIZE, PAGE_SIZE, "s2 L1 table");
        rsv_block(PAGE_SIZE, PAGE_SIZE, "s2 L1 table");
        /* L0 tables: 4KB each, up to 8 per partition */
        for (j = 0; j < 8; j++)
            rsv_block(PAGE_SIZE, PAGE_SIZE, "s2 L0 table");
    }
}

void arch_loader_rsv_mem(FILE *out_file) {
    /* No loader reserved memory needed for riscv64 */
}
