/*
 * FILE: mem.c
 *
 * System physical memory
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <hypercalls.h>
#include <objdir.h>
#include <physmm.h>
#include <sched.h>
#include <stdc.h>

#include <objects/mem.h>

static inline prtos_s32_t copy_area(prtos_address_t dst_addr, prtos_id_t dst_id, prtos_address_t src_addr, prtos_id_t src_id, prtos_s_size_t size) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_u32_t flags;

    if (size <= 0) return 0;
    if (dst_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id))
        if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if (src_id != KID2PARTID(info->sched.current_kthread->ctrl.g->id))
        if (!(get_partition(info->sched.current_kthread)->cfg->flags & PRTOS_PART_SYSTEM)) return PRTOS_PERM_ERROR;

    if (dst_id != UNCHECKED_ID) {
        if ((dst_id < 0) || (dst_id >= prtos_conf_table.num_of_partitions)) return PRTOS_INVALID_PARAM;

        if (!phys_mm_find_area(dst_addr, size, (dst_id != UNCHECKED_ID) ? &part_table[dst_id] : 0, &flags)) return PRTOS_INVALID_PARAM;

        if (flags & PRTOS_MEM_AREA_READONLY) return PRTOS_INVALID_PARAM;
    }

    if (src_id != UNCHECKED_ID) {
        if ((src_id < 0) || (src_id >= prtos_conf_table.num_of_partitions)) return PRTOS_INVALID_PARAM;

        if (!phys_mm_find_area(src_addr, size, (src_id != UNCHECKED_ID) ? &part_table[src_id] : 0, &flags)) return PRTOS_INVALID_PARAM;
    }

    if (size <= 0) return 0;

    unalign_memcpy((void *)dst_addr, (void *)src_addr, size, (rd_mem_t)read_by_pass_mmu_word, (rd_mem_t)read_by_pass_mmu_word,
                   (wr_mem_t)write_by_pass_mmu_word);

    return size;
}

static prtos_s32_t ctrl_mem(prtos_obj_desc_t desc, prtos_u32_t cmd, union mem_cmd *__g_param args) {
    if (!args) return PRTOS_INVALID_PARAM;

    if (check_gp_aram(args, sizeof(union mem_cmd), 4, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    switch (cmd) {
        case PRTOS_OBJ_MEM_CPY_AREA:
            return copy_area(args->cpy_area.dst_addr, args->cpy_area.dst_id, args->cpy_area.src_addr, args->cpy_area.src_id, args->cpy_area.size);
    }

    return PRTOS_INVALID_PARAM;
}

static const struct object mem_obj = {
    .ctrl = (ctrl_obj_op_t)ctrl_mem,
};

prtos_s32_t __VBOOT setup_mem(void) {
    object_table[OBJ_CLASS_MEM] = &mem_obj;
    return 0;
}

REGISTER_OBJ(setup_mem);
