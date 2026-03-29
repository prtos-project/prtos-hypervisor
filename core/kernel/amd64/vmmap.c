/*
 * FILE: vmmap.c
 *
 * Virtual memory map management for amd64
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
    prtos_word_t *pml4, *pdpt, *pd_table;

    st = prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].start_addr;
    end = st + prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].size - 1;

    *start_frame_area = ROUNDUP(_PHYS2VIRT(end + 1), LPAGE_SIZE);
    *num_of_frames = ((PRTOS_VMAPEND - *start_frame_area) + 1) / PAGE_SIZE;

    /* Walk the 4-level page tables: PML4 → PDPT → PD
     * Find the PD used for the kernel virtual mapping at CONFIG_PRTOS_OFFSET */
    pml4 = (prtos_word_t *)_PHYS2VIRT(save_cr3());
    pdpt = (prtos_word_t *)_PHYS2VIRT(pml4[PML4_INDEX(CONFIG_PRTOS_OFFSET)] & PAGE_MASK);
    pd_table = (prtos_word_t *)_PHYS2VIRT(pdpt[PDPT_INDEX(CONFIG_PRTOS_OFFSET)] & PAGE_MASK);

    /* Map the hypervisor physical memory with 2MB pages in the PD */
    for (page = _PHYS2VIRT(st); page < _PHYS2VIRT(end); page += LPAGE_SIZE) {
        pd_table[PD_INDEX(page)] = (_VIRT2PHYS(page) & LPAGE_MASK) | _PG_ARCH_PRESENT | _PG_ARCH_PSE | _PG_ARCH_RW | _PG_ARCH_GLOBAL;
    }


    /* Also maintain the identity mapping in the low PD (PDPT[0]) */
    {
        prtos_word_t *pd_identity = (prtos_word_t *)_PHYS2VIRT(pdpt[0] & PAGE_MASK);
        for (page = st; page < end; page += LPAGE_SIZE) {
            pd_identity[PD_INDEX(page)] = (page & LPAGE_MASK) | _PG_ARCH_PRESENT | _PG_ARCH_PSE | _PG_ARCH_RW | _PG_ARCH_GLOBAL;
        }
    }


    flush_tlb();

    /* Allocate page tables (PTs) for the vcache frame area */
    num_of_pages = (((PRTOS_VMAPEND - *start_frame_area) + 1) >> PD_SHIFT);
    GET_MEMAZ(rsv_pages, PTDL2SIZE * num_of_pages, PTDL2SIZE);

    for (e = PD_INDEX(*start_frame_area); (e < PTDL1ENTRIES) && (num_of_pages > 0); e++) {
        ASSERT(num_of_pages >= 0);
        pd_table[e] = (_VIRT2PHYS(rsv_pages) & PAGE_MASK) | _PG_ARCH_PRESENT | _PG_ARCH_RW;
        rsv_pages = (prtos_address_t *)((prtos_address_t)rsv_pages + PTDL2SIZE);
        num_of_pages--;
    }
    flush_tlb_global();
}

void setup_ptd_level_1_table(prtos_word_t *ptd_level_1, kthread_t *k) {
    /* ptd_level_1 is the partition's PML4 (mapped via enable_by_pass_mmu).
     * vm_map_user_page already created PML4[0]→PDPT with entries for both
     * the low partition region (PDPT[0]) and the high PCT/PBL region (PDPT[3]).
     * We need to copy the kernel's PD entries for CONFIG_PRTOS_OFFSET+
     * into the partition's PD for PDPT[3].
     */
    prtos_word_t pml4e, pdpte;
    prtos_address_t pdpt_phys, pd_phys;
    prtos_word_t *hyp_pml4, *hyp_pdpt, *hyp_pd;
    volatile prtos_word_t *pd_part;
    prtos_s32_t e;

    /* On amd64, partition page tables (PDPT/PD) are in partition physical memory
     * (identity-mapped in first 1GB). We must ensure the hypervisor's CR3 is active
     * so the identity map is available. Also, use the known hypervisor PML4 address
     * directly (not save_cr3(), which might return a partition CR3 if called from
     * a hypercall or after schedule()). */
    prtos_u64_t _saved_cr3 = save_cr3();
    load_hyp_page_table();

    pml4e = ptd_level_1[PML4_INDEX(CONFIG_PRTOS_OFFSET)];
    if (!(pml4e & _PG_ARCH_PRESENT)) {
        load_cr3(_saved_cr3);
        return;
    }

    pdpt_phys = pml4e & PAGE_MASK;
    /* PDPT is in partition physical memory, identity-mapped in first 1GB */
    pdpte = *(volatile prtos_word_t *)(unsigned long)(pdpt_phys + PDPT_INDEX(CONFIG_PRTOS_OFFSET) * sizeof(prtos_word_t));
    if (!(pdpte & _PG_ARCH_PRESENT)) {
        load_cr3(_saved_cr3);
        return;
    }

    pd_phys = pdpte & PAGE_MASK;
    pd_part = (volatile prtos_word_t *)(unsigned long)pd_phys;

    /* Get kernel PD from the hypervisor's page tables (use known PML4 address) */
    hyp_pml4 = (prtos_word_t *)(unsigned long)_page_tables;
    hyp_pdpt = (prtos_word_t *)_PHYS2VIRT(hyp_pml4[PML4_INDEX(CONFIG_PRTOS_OFFSET)] & PAGE_MASK);
    hyp_pd = (prtos_word_t *)_PHYS2VIRT(hyp_pdpt[PDPT_INDEX(CONFIG_PRTOS_OFFSET)] & PAGE_MASK);

    /* Copy kernel PD entries from CONFIG_PRTOS_OFFSET upward.
     * Partition entries (PCT/PBL) are at PD indices below PD_INDEX(CONFIG_PRTOS_OFFSET)
     * so they won't be overwritten. */
    for (e = PD_INDEX(CONFIG_PRTOS_OFFSET); e < 512; e++) {
        pd_part[e] = hyp_pd[e];
    }

    load_cr3(_saved_cr3);
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

prtos_s32_t vm_map_user_page(partition_t *k, prtos_word_t *pml4, prtos_address_t p_addr, prtos_address_t v_addr, prtos_u32_t flags,
                             prtos_address_t (*alloc)(struct prtos_conf_part *, prtos_u_size_t, prtos_u32_t, prtos_address_t *, prtos_s_size_t *),
                             prtos_address_t *pool, prtos_s_size_t *pool_size) {
    struct phys_page *page;
    prtos_address_t p_table;
    prtos_s32_t e;
    prtos_s32_t pml4e = PML4_INDEX(v_addr);
    prtos_s32_t pdpte = PDPT_INDEX(v_addr);
    prtos_s32_t pde = PD_INDEX(v_addr);
    prtos_s32_t pte = PT_INDEX(v_addr);

    ASSERT(!(p_addr & (PAGE_SIZE - 1)));
    ASSERT(!(v_addr & (PAGE_SIZE - 1)));
    ASSERT(v_addr < CONFIG_PRTOS_OFFSET);

    /* --- Level 1: PML4 → PDPT --- */
    /* pml4 is vcache-mapped; subsequent levels are identity-mapped (partition phys < 1GB) */
    if (!(pml4[pml4e] & _PG_ARCH_PRESENT)) {
        p_table = alloc(k->cfg, PAGE_SIZE, PAGE_SIZE, pool, pool_size);
        if (!(page = pmm_find_page(p_table, k, 0))) return -1;
        page->type = PPAG_PTDL1;
        phys_page_inc_counter(page);
        {
            volatile prtos_word_t *p = (volatile prtos_word_t *)(unsigned long)p_table;
            for (e = 0; e < 512; e++) p[e] = 0;
        }
        pml4[pml4e] = p_table | _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_USER;
    }

    volatile prtos_word_t *pdpt = (volatile prtos_word_t *)(unsigned long)(pml4[pml4e] & PAGE_MASK);

    /* --- Level 2: PDPT → PD --- */
    if (!(pdpt[pdpte] & _PG_ARCH_PRESENT)) {
        p_table = alloc(k->cfg, PAGE_SIZE, PAGE_SIZE, pool, pool_size);
        if (!(page = pmm_find_page(p_table, k, 0))) return -1;
        page->type = PPAG_PTDL1;
        phys_page_inc_counter(page);
        {
            volatile prtos_word_t *p = (volatile prtos_word_t *)(unsigned long)p_table;
            for (e = 0; e < 512; e++) p[e] = 0;
        }
        pdpt[pdpte] = p_table | _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_USER;
    }

    volatile prtos_word_t *pd = (volatile prtos_word_t *)(unsigned long)(pdpt[pdpte] & PAGE_MASK);

    /* --- Level 3: PD → PT --- */
    if (!(pd[pde] & _PG_ARCH_PRESENT)) {
        p_table = alloc(k->cfg, PTDL2SIZE, PTDL2SIZE, pool, pool_size);
        if (!(page = pmm_find_page(p_table, k, 0))) return -1;
        page->type = PPAG_PTDL2;
        phys_page_inc_counter(page);
        {
            volatile prtos_word_t *p = (volatile prtos_word_t *)(unsigned long)p_table;
            for (e = 0; e < PTDL2ENTRIES; e++) p[e] = 0;
        }
        pd[pde] = p_table | _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_USER;
    }

    volatile prtos_word_t *pt = (volatile prtos_word_t *)(unsigned long)(pd[pde] & PAGE_MASK);

    /* --- Level 4: PT → page --- */
    pt[pte] = (p_addr & PAGE_MASK) | vm_attr_to_arch_attr(flags);

    return 0;
}

void vm_map_page(prtos_address_t p_addr, prtos_address_t v_addr, prtos_word_t flags) {
    prtos_word_t *pml4, *pdpt, *pd, *pt;

    ASSERT(!(p_addr & (PAGE_SIZE - 1)));
    ASSERT(!(v_addr & (PAGE_SIZE - 1)));
    ASSERT(v_addr >= CONFIG_PRTOS_OFFSET);

    /* Walk the 4-level page tables to find the PT entry */
    pml4 = (prtos_word_t *)_PHYS2VIRT(save_cr3());
    ASSERT(pml4[PML4_INDEX(v_addr)] & _PG_ARCH_PRESENT);
    pdpt = (prtos_word_t *)_PHYS2VIRT(pml4[PML4_INDEX(v_addr)] & PAGE_MASK);
    ASSERT(pdpt[PDPT_INDEX(v_addr)] & _PG_ARCH_PRESENT);
    pd = (prtos_word_t *)_PHYS2VIRT(pdpt[PDPT_INDEX(v_addr)] & PAGE_MASK);
    ASSERT((pd[PD_INDEX(v_addr)] & _PG_ARCH_PRESENT) && !(pd[PD_INDEX(v_addr)] & _PG_ARCH_PSE));
    pt = (prtos_word_t *)_PHYS2VIRT(pd[PD_INDEX(v_addr)] & PAGE_MASK);
    pt[PT_INDEX(v_addr)] = p_addr | vm_attr_to_arch_attr(flags);
    flush_tlb_entry(v_addr);
}
