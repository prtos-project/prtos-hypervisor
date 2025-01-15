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
    prtos_address_t st, end, page;
    prtos_address_t *rsv_pages;
    prtos_s32_t e, num_of_pages;
    prtos_u32_t *page_table;

    st = prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].start_addr;
    end = st + prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].size - 1;

    *start_frame_area = ROUNDUP(_PHYS2VIRT(end + 1), LPAGE_SIZE);
    *num_of_frames = ((PRTOS_VMAPEND - *start_frame_area) + 1) / PAGE_SIZE;

    page_table = (prtos_u32_t *)_PHYS2VIRT(save_cr3());
    for (page = _PHYS2VIRT(st); page < _PHYS2VIRT(end); page += LPAGE_SIZE) {
        page_table[VADDR_TO_PDE_INDEX(page)] = (_VIRT2PHYS(page) & LPAGE_MASK) | _PG_ARCH_PRESENT | _PG_ARCH_PSE | _PG_ARCH_RW | _PG_ARCH_GLOBAL;
        page_table[VADDR_TO_PDE_INDEX(_VIRT2PHYS(page))] = (_VIRT2PHYS(page) & LPAGE_MASK) | _PG_ARCH_PRESENT | _PG_ARCH_PSE | _PG_ARCH_RW | _PG_ARCH_GLOBAL;
    }

    flush_tlb(); /* The prtos mappings have changed. This is required to update the TLB entries */

    num_of_pages = (((PRTOS_VMAPEND - *start_frame_area) + 1) >> PTDL1_SHIFT);
    GET_MEMAZ(rsv_pages, PTDL2SIZE * num_of_pages, PTDL2SIZE);

    for (e = VADDR_TO_PDE_INDEX(*start_frame_area); (e < PTDL1ENTRIES) && (num_of_pages > 0); e++) {
        ASSERT(num_of_pages >= 0);
        page_table[e] = (_VIRT2PHYS(rsv_pages) & PAGE_MASK) | _PG_ARCH_PRESENT | _PG_ARCH_RW;
        rsv_pages = (prtos_address_t *)((prtos_address_t)rsv_pages + PTDL2SIZE);
        num_of_pages--;
    }
    flush_tlb_global();
}

void setup_ptd_level_1_table(prtos_word_t *ptd_level_1, kthread_t *k) {
    prtos_s32_t l1e, e;

    l1e = VADDR_TO_PDE_INDEX(CONFIG_PRTOS_OFFSET);
    for (e = l1e; e < PTDL1ENTRIES; e++) {
        ptd_level_1[e] = _page_tables[e];
    }
}

prtos_u32_t vm_arch_attr_to_attr(prtos_u32_t entry) {
    prtos_u32_t flags = entry & (PAGE_SIZE - 1), attr = 0;

    if (flags & _PG_ARCH_PRESENT) attr |= _PG_ATTR_PRESENT;
    if (flags & _PG_ARCH_USER) attr |= _PG_ATTR_USER;
    if (flags & _PG_ARCH_RW) attr |= _PG_ATTR_RW;
    if (!(flags & _PG_ARCH_PCD)) attr |= _PG_ATTR_CACHED;
    return attr | (flags & ~(_PG_ARCH_PRESENT | _PG_ARCH_USER | _PG_ARCH_RW | _PG_ARCH_PCD));
}

prtos_u32_t vm_attr_to_arch_attr(prtos_u32_t flags) {
    prtos_u32_t attr = 0;

    if (flags & _PG_ATTR_PRESENT) attr |= _PG_ARCH_PRESENT;
    if (flags & _PG_ATTR_USER) attr |= _PG_ARCH_USER;
    if (flags & _PG_ATTR_RW) attr |= _PG_ARCH_RW;
    if (!(flags & _PG_ATTR_CACHED)) attr |= _PG_ARCH_PCD;
    return attr | (flags & 0xffff);
}

prtos_s32_t vm_map_user_page(partition_t *k, prtos_word_t *ptd_level_1, prtos_address_t p_addr, prtos_address_t v_addr, prtos_u32_t flags,
                             prtos_address_t (*alloc)(struct prtos_conf_part *, prtos_u_size_t, prtos_u32_t, prtos_address_t *, prtos_s_size_t *),
                             prtos_address_t *pool, prtos_s_size_t *pool_size) {
    struct phys_page *page_table_level2;
    prtos_address_t p_table;
    prtos_word_t *p_ptd_level_2;
    prtos_s32_t l1e, l2e, e;

    ASSERT(!(p_addr & (PAGE_SIZE - 1)));
    ASSERT(!(v_addr & (PAGE_SIZE - 1)));
    ASSERT(v_addr < CONFIG_PRTOS_OFFSET);
    l1e = VADDR_TO_PDE_INDEX(v_addr);
    l2e = VADDR_TO_PTE_INDEX(v_addr);
    if (!(ptd_level_1[l1e] & _PG_ARCH_PRESENT)) {
        p_table = alloc(k->cfg, PTDL2SIZE, PTDL2SIZE, pool, pool_size);
        if (!(page_table_level2 = pmm_find_page(p_table, k, 0))) {
            return -1;
        }
        page_table_level2->type = PPAG_PTDL2;
        phys_page_inc_counter(page_table_level2);
        p_ptd_level_2 = vcache_map_page(p_table, page_table_level2);
        for (e = 0; e < PTDL2ENTRIES; ++e) {
            p_ptd_level_2[e] = 0;
        }
        ptd_level_1[l1e] = p_table | _PG_ARCH_PRESENT | _PG_ARCH_RW;
    } else {
        p_table = ptd_level_1[l1e] & PAGE_MASK;
        page_table_level2 = pmm_find_page(p_table, k, 0);
        ASSERT(page_table_level2);
        ASSERT(page_table_level2->type == PPAG_PTDL2);
        ASSERT(page_table_level2->counter > 0);
        p_ptd_level_2 = vcache_map_page(p_table, page_table_level2);
    }
    p_ptd_level_2[l2e] = (p_addr & PAGE_MASK) | vm_attr_to_arch_attr(flags);
    vcache_unlock_page(page_table_level2);

    return 0;
}

void vm_map_page(prtos_address_t p_addr, prtos_address_t v_addr, prtos_word_t flags) {
    prtos_address_t *page_table_entry;

    ASSERT(!(p_addr & (PAGE_SIZE - 1)));
    ASSERT(!(v_addr & (PAGE_SIZE - 1)));
    ASSERT(v_addr >= CONFIG_PRTOS_OFFSET);

    ASSERT((_page_tables[VADDR_TO_PDE_INDEX(v_addr)] & _PG_ARCH_PRESENT) == _PG_ARCH_PRESENT);

    page_table_entry = (prtos_address_t *)_PHYS2VIRT(_page_tables[VADDR_TO_PDE_INDEX(v_addr)] & PAGE_MASK);
    page_table_entry[VADDR_TO_PTE_INDEX(v_addr)] = p_addr | vm_attr_to_arch_attr(flags);
    flush_tlb_entry(v_addr);
}
