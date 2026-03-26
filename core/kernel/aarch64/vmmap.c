/*
 * FILE: vmmap.c
 *
 * Virtual memory map management
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <kthread.h>
#include <rsvmem.h>
#include <stdc.h>
#include <vmmap.h>
#include <virtmm.h>
#include <physmm.h>
#include <smp.h>
#include <arch/prtos_def.h>
#include <arch/processor.h>

void __VBOOT setup_vm_map(prtos_address_t *start_frame_area, prtos_s32_t *num_of_frames) {
     *start_frame_area = 0x00000a0040000000;
     *num_of_frames = 0x40000000 / PAGE_SIZE; // 1GB
}

void setup_ptd_level_1_table(prtos_word_t *ptd_level_1, kthread_t *k) {
}

prtos_u32_t vm_arch_attr_to_attr(prtos_u32_t entry) {
    prtos_u32_t flags = entry & (PAGE_SIZE - 1), attr = 0;
    return attr | (flags & ~(_PG_ARCH_PRESENT | _PG_ARCH_USER | _PG_ARCH_RW | _PG_ARCH_PCD));
}

prtos_u32_t vm_attr_to_arch_attr(prtos_u32_t flags) {
    prtos_u32_t attr = 0;
    return attr | (flags & 0xffff);
}

prtos_s32_t vm_map_user_page(partition_t *k, prtos_word_t *ptd_level_1, prtos_address_t p_addr, prtos_address_t v_addr, prtos_u32_t flags,
                             prtos_address_t (*alloc)(struct prtos_conf_part *, prtos_u_size_t, prtos_u32_t, prtos_address_t *, prtos_s_size_t *),
                             prtos_address_t *pool, prtos_s_size_t *pool_size) {

    return 0;
}

void vm_map_page(prtos_address_t p_addr, prtos_address_t v_addr, prtos_word_t flags) {
}
