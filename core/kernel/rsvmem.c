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
#if defined(CONFIG_AARCH64)
    eprintf("------------init_rsv_mem----------------------\n");
#endif
    for (e = 0; prtos_conf_rsv_mem_table[e].obj; e++) {
#if defined(CONFIG_AARCH64)
        eprintf("00 prtos_conf_rsv_mem_table[%d].obj:0x%llx\n", e, prtos_conf_rsv_mem_table[e].obj);
        eprintf("11 prtos_conf_rsv_mem_table[%d].obj:0x%llx\n", e, prtos_conf_rsv_mem_table[e].obj + (prtos_address_t)&prtos_conf_table);
        eprintf("prtos_conf_rsv_mem_table[%d].used_align:0x%x\n", e, prtos_conf_rsv_mem_table[e].used_align);
        eprintf("prtos_conf_rsv_mem_table[%d].size:%x\n", e, prtos_conf_rsv_mem_table[e].size);
#endif
        prtos_conf_rsv_mem_table[e].used_align &= ~RSV_MEM_USED;
    }
#if defined(CONFIG_AARCH64)
    eprintf("------------init_rsv_mem----------------------\n");
#endif
}

void *alloc_rsv_mem(prtos_u32_t size, prtos_u32_t align) {
    prtos_s32_t e;
    eprintf("alloc_rsv_mem: size: %d, align: 0x%x\n", size, align);
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
            PWARN("Reserved memory not used %d:%d:0x%llx\n", prtos_conf_rsv_mem_table[e].used_align, prtos_conf_rsv_mem_table[e].size,
                  prtos_conf_rsv_mem_table[e].obj);
        }
}
#endif
