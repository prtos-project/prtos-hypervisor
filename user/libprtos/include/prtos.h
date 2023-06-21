/*
 * FILE: prtos.h
 *
 * Guest header file
 *
 * www.prtos.org
 */

#ifndef _LIB_PRTOS_H_
#define _LIB_PRTOS_H_

#ifdef _PRTOS_KERNEL_
#error Guest file, do not include.
#endif

#include <prtos_inc/config.h>
#include <prtos_inc/arch/arch_types.h>
#include <prtos_inc/prtosef.h>
#include <prtoshypercalls.h>

#ifndef __ASSEMBLY__

#include <prtos_inc/hypercalls.h>
#include <prtos_inc/guest.h>

extern struct lib_prtos_params {
    partition_control_table_t *part_ctrl_table[CONFIG_MAX_NO_VCPUS];
    struct prtos_physical_mem_map *part_mem_map[CONFIG_MAX_NO_VCPUS];
    prtos_word_t *comm_port_bitmap[CONFIG_MAX_NO_VCPUS];
} lib_prtos_params;

extern __stdcall void init_libprtos(partition_control_table_t *part_ctrl_table);

static inline struct prtos_physical_mem_map *prtos_get_partition_mmap(void) {
    return lib_prtos_params.part_mem_map[prtos_get_vcpuid()];
}

static inline partition_control_table_t *prtos_get_pct(void) {
    return lib_prtos_params.part_ctrl_table[prtos_get_vcpuid()];
}

static inline partition_control_table_t *prtos_get_pct0(void) {
    return lib_prtos_params.part_ctrl_table[0];
}

static inline prtos_word_t *prtos_get_commport_bitmap(void) {
    return lib_prtos_params.comm_port_bitmap[prtos_get_vcpuid()];
}

static inline prtos_id_t prtos_get_number_vcpus(void) {
    return prtos_get_pct0()->num_of_vcpus;
}

#include <comm.h>
#include <hm.h>
#include <hypervisor.h>
#include <trace.h>
#include <status.h>

#endif

#endif
