/*
 * FILE: init.c
 *
 * Initialisation of the libprtos
 *
 * www.prtos.org
 */

#include <prtos.h>

struct lib_prtos_params lib_prtos_params;

__stdcall void init_libprtos(partition_control_table_t *part_ctrl_table) {
    prtos_s32_t e = prtos_get_vcpuid();

    lib_prtos_params.part_ctrl_table[e] = (partition_control_table_t *)((prtos_address_t)part_ctrl_table + (part_ctrl_table->part_ctrl_table_size * e));
    lib_prtos_params.part_mem_map[e] = (struct prtos_physical_mem_map *)((prtos_address_t)lib_prtos_params.part_ctrl_table[e] + sizeof(partition_control_table_t));
    lib_prtos_params.comm_port_bitmap[e] =
        (prtos_word_t *)((prtos_address_t)lib_prtos_params.part_mem_map[e] +
                         lib_prtos_params.part_ctrl_table[e]->num_of_physical_mem_areas * sizeof(struct prtos_physical_mem_map));

    init_batch();
    init_arch_libprtos();
}
