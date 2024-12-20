/*
 * FILE: rsvmem.c
 *
 * Memory for structures
 *
 * www.prtos.org
 */

#include <prtosconf.h>
#include <assert.h>
#include <processor.h>

void init_rsv_mem(void) {
    prtos_s32_t e;
    for (e = 0; prtos_conf_rsv_mem_table[e].obj; e++) prtos_conf_rsv_mem_table[e].used_align &= ~RSV_MEM_USED;
}

void *alloc_rsv_mem(prtos_u32_t size, prtos_u32_t align) {
    prtos_s32_t e;
    for (e = 0; prtos_conf_rsv_mem_table[e].obj; e++) {
        if (!(prtos_conf_rsv_mem_table[e].used_align & RSV_MEM_USED) && ((prtos_conf_rsv_mem_table[e].used_align & ~RSV_MEM_USED) == align) &&
            (prtos_conf_rsv_mem_table[e].size == size)) {
            prtos_conf_rsv_mem_table[e].used_align |= RSV_MEM_USED;
            return (void *)((prtos_address_t)prtos_conf_rsv_mem_table[e].obj + (prtos_address_t)&prtos_conf_table);
        }
    }
    return 0;
}

#ifdef CONFIG_DEBUG
void rsv_mem_debug(void) {
    prtos_s32_t e;
    for (e = 0; prtos_conf_rsv_mem_table[e].obj; e++)
        if (!(prtos_conf_rsv_mem_table[e].used_align & RSV_MEM_USED)) {
            system_panic("Reserved memory not used %d:%d:0x%x\n", prtos_conf_rsv_mem_table[e].used_align, prtos_conf_rsv_mem_table[e].size,
                         prtos_conf_rsv_mem_table[e].obj);
        }
}
#endif
