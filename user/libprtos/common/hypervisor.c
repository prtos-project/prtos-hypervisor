/*
 * FILE: hypervisor.c
 *
 * Hypervisor related functions
 *
 * www.prtos.org
 */

#include <prtos.h>
#include <hypervisor.h>
#include <prtoshypercalls.h>
#include <arch/atomic_ops.h>

#include <prtos_inc/bitwise.h>
#include <prtos_inc/linkage.h>
#include <prtos_inc/hypercalls.h>
#include <prtos_inc/objdir.h>
#include <prtos_inc/objects/mem.h>

__stdcall prtos_s32_t prtos_write_console(char *buffer, prtos_s32_t length) {
    return prtos_write_object(OBJDESC_BUILD(OBJ_CLASS_CONSOLE, PRTOS_PARTITION_SELF, 0), buffer, length, 0);
}

__stdcall prtos_s32_t prtos_memory_copy(prtos_id_t dst_id, prtos_u32_t dst_addr, prtos_id_t src_id, prtos_u32_t src_addr, prtos_u32_t size) {
    union mem_cmd args;
    args.cpy_area.dst_id = dst_id;
    args.cpy_area.src_id = src_id;
    args.cpy_area.dst_addr = dst_addr;
    args.cpy_area.src_addr = src_addr;
    args.cpy_area.size = size;
    return prtos_ctrl_object(OBJDESC_BUILD(OBJ_CLASS_MEM, 0, 0), PRTOS_OBJ_MEM_CPY_AREA, (void *)&args);
}
