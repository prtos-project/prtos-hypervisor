/* PRTOS memory management - consolidated */
/* === BEGIN INLINED: arch_arm_arm64_mmu_mm.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */

#include <prtos_init.h>
#include <prtos_mm.h>
#include <prtos_pfn.h>

#include <asm_setup.h>
#include <asm_static-memory.h>
#include <asm_static-shmem.h>

/* Override macros from asm/page.h to make them work with mfn_t */
#undef virt_to_mfn
#define virt_to_mfn(va) _mfn(__virt_to_mfn(va))

static DEFINE_PAGE_TABLE(prtos_first_id);
static DEFINE_PAGE_TABLE(prtos_second_id);
static DEFINE_PAGE_TABLE(prtos_third_id);

extern char start[], end[];

/*
 * The identity mapping may start at physical address 0. So we don't want
 * to keep it mapped longer than necessary.
 *
 * When this is called, we are still using the boot_pgtable.
 *
 * We need to prepare the identity mapping for both the boot page tables
 * and runtime page tables.
 *
 * The logic to create the entry is slightly different because PRTOS may
 * be running at a different location at runtime.
 */
static void __init prepare_boot_identity_mapping(void)
{
    paddr_t id_addr = virt_to_maddr(start);
    lpae_t pte;
    DECLARE_OFFSETS(id_offsets, id_addr);

    /*
     * We will be re-using the boot ID tables. They may not have been
     * zeroed but they should be unlinked. So it is fine to use
     * clear_page().
     */
    clear_page(boot_first_id);
    clear_page(boot_second_id);
    clear_page(boot_third_id);

    if ( id_offsets[0] >= IDENTITY_MAPPING_AREA_NR_L0 )
        panic("Cannot handle ID mapping above %uTB\n",
              IDENTITY_MAPPING_AREA_NR_L0 >> 1);

    /* Link first ID table */
    pte = mfn_to_prtos_entry(virt_to_mfn(boot_first_id), MT_NORMAL);
    pte.pt.table = 1;
    pte.pt.xn = 0;

    write_pte(&boot_pgtable[id_offsets[0]], pte);

    /* Link second ID table */
    pte = mfn_to_prtos_entry(virt_to_mfn(boot_second_id), MT_NORMAL);
    pte.pt.table = 1;
    pte.pt.xn = 0;

    write_pte(&boot_first_id[id_offsets[1]], pte);

    /* Link third ID table */
    pte = mfn_to_prtos_entry(virt_to_mfn(boot_third_id), MT_NORMAL);
    pte.pt.table = 1;
    pte.pt.xn = 0;

    write_pte(&boot_second_id[id_offsets[2]], pte);

    /* The mapping in the third table will be created at a later stage */
}



static void __init prepare_runtime_identity_mapping(void)
{
    paddr_t id_addr = virt_to_maddr(start);
    lpae_t pte;
    DECLARE_OFFSETS(id_offsets, id_addr);

    if ( id_offsets[0] >= IDENTITY_MAPPING_AREA_NR_L0 )
        panic("Cannot handle ID mapping above %uTB\n",
              IDENTITY_MAPPING_AREA_NR_L0 >> 1);

    /* Link first ID table */
    pte = pte_of_prtosaddr((vaddr_t)prtos_first_id);
    pte.pt.table = 1;
    pte.pt.xn = 0;

    write_pte(&prtos_pgtable[id_offsets[0]], pte);

    /* Link second ID table */
    pte = pte_of_prtosaddr((vaddr_t)prtos_second_id);
    pte.pt.table = 1;
    pte.pt.xn = 0;

    write_pte(&prtos_first_id[id_offsets[1]], pte);

    /* Link third ID table */
    pte = pte_of_prtosaddr((vaddr_t)prtos_third_id);
    pte.pt.table = 1;
    pte.pt.xn = 0;

    write_pte(&prtos_second_id[id_offsets[2]], pte);

    /* The mapping in the third table will be created at a later stage */
}

void __init arch_setup_page_tables(void)
{
    prepare_boot_identity_mapping();
    prepare_runtime_identity_mapping();
}

void update_identity_mapping(bool enable)
{
    paddr_t id_addr = virt_to_maddr(start);
    // eprintf("update_identity_mapping: id_addr=0x%llx\n", id_addr);
    printk("printk update_identity_mapping: id_addr=0x%lx\n", id_addr);
    int rc;

    if ( enable )
        rc = map_pages_to_prtos(id_addr, maddr_to_mfn(id_addr), 1,
                              PAGE_HYPERVISOR_RX);
    else
        rc = destroy_prtos_mappings(id_addr, id_addr + PAGE_SIZE);

    BUG_ON(rc);
}

extern void switch_ttbr_id(uint64_t ttbr);

typedef void (switch_ttbr_fn)(uint64_t ttbr);

void __init switch_ttbr(uint64_t ttbr)
{
    vaddr_t id_addr = virt_to_maddr(switch_ttbr_id);
    switch_ttbr_fn *fn = (switch_ttbr_fn *)id_addr;
    lpae_t pte;

    /* Enable the identity mapping in the boot page tables */
    update_identity_mapping(true);

    /* Enable the identity mapping in the runtime page tables */
    pte = pte_of_prtosaddr((vaddr_t)switch_ttbr_id);
    pte.pt.table = 1;
    pte.pt.xn = 0;
    pte.pt.ro = 1;
    write_pte(&prtos_third_id[third_table_offset(id_addr)], pte);

    /* Switch TTBR */
    fn(ttbr);

    /*
     * Disable the identity mapping in the runtime page tables.
     * Note it is not necessary to disable it in the boot page tables
     * because they are not going to be used by this CPU anymore.
     */
    update_identity_mapping(false);
}

/* Map the region in the directmap area. */
static void __init setup_directmap_mappings(unsigned long base_mfn,
                                            unsigned long nr_mfns)
{
    int rc;

    /* First call sets the directmap physical and virtual offset. */
    if ( mfn_eq(directmap_mfn_start, INVALID_MFN) )
    {
        unsigned long mfn_gb = base_mfn & ~((FIRST_SIZE >> PAGE_SHIFT) - 1);

        directmap_mfn_start = _mfn(base_mfn);
        directmap_base_pdx = mfn_to_pdx(_mfn(base_mfn));
        /*
         * The base address may not be aligned to the first level
         * size (e.g. 1GB when using 4KB pages). This would prevent
         * superpage mappings for all the regions because the virtual
         * address and machine address should both be suitably aligned.
         *
         * Prevent that by offsetting the start of the directmap virtual
         * address.
         */
        directmap_virt_start = DIRECTMAP_VIRT_START +
            (base_mfn - mfn_gb) * PAGE_SIZE;
    }

    if ( base_mfn < mfn_x(directmap_mfn_start) )
        panic("cannot add directmap mapping at %lx below heap start %lx\n",
              base_mfn, mfn_x(directmap_mfn_start));

    rc = map_pages_to_prtos((vaddr_t)__mfn_to_virt(base_mfn),
                          _mfn(base_mfn), nr_mfns,
                          PAGE_HYPERVISOR_RW | _PAGE_BLOCK);
    if ( rc )
        panic("Unable to setup the directmap mappings.\n");
}

void __init setup_mm(void)
{
    const struct membanks *banks __attribute__((unused)) = bootinfo_get_mem();
    paddr_t ram_start = INVALID_PADDR;
    paddr_t ram_end = 0;
    paddr_t ram_size = 0;
    unsigned int i __attribute__((unused));

    init_pdx();

    /*
     * We need some memory to allocate the page-tables used for the directmap
     * mappings. But some regions may contain memory already allocated
     * for other uses (e.g. modules, reserved-memory...).
     *
     * For simplicity, add all the free regions in the boot allocator.
     */
    populate_boot_allocator();

    total_pages = 0;

    // for ( i = 0; i < banks->nr_banks; i++ )
    // {
    //     const struct membank *bank = &banks->bank[i];
    //     paddr_t bank_end = bank->start + bank->size;

    //     ram_size = ram_size + bank->size;
    //     ram_start = min(ram_start, bank->start);
    //     ram_end = max(ram_end, bank_end);

    //     setup_directmap_mappings(PFN_DOWN(bank->start),
    //                              PFN_DOWN(bank->size));
    // }
#if 1 // WA for PRTOS
   ram_size = 0x100000000;
   ram_start = 0x40000000;
   ram_end = 0x140000000;
   setup_directmap_mappings(PFN_DOWN(ram_start),
                            PFN_DOWN(ram_size));
#endif
    total_pages += ram_size >> PAGE_SHIFT;

    directmap_virt_end = PRTOSHEAP_VIRT_START + ram_end - ram_start;
    directmap_mfn_start = maddr_to_mfn(ram_start);
    directmap_mfn_end = maddr_to_mfn(ram_end);

    setup_frametable_mappings(ram_start, ram_end);
    max_page = PFN_DOWN(ram_end);

    init_staticmem_pages();
    init_sharedmem_pages();
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_arm64_mmu_mm.c === */
/* === BEGIN INLINED: arch_arm_mmu_p2m.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
#include <prtos_cpu.h>
#include <prtos_domain_page.h>
#include <prtos_ioreq.h>
#include <prtos_lib.h>
#include <prtos_sched.h>
#include <prtos_softirq.h>

#include <asm_alternative.h>
#include <asm_event.h>
#include <asm_flushtlb.h>
#include <asm_page.h>

unsigned int __read_mostly p2m_root_order;
unsigned int __read_mostly p2m_root_level;

static mfn_t __read_mostly empty_root_mfn;

static uint64_t generate_vttbr(uint16_t vmid, mfn_t root_mfn) {
    return (mfn_to_maddr(root_mfn) | ((uint64_t)vmid << 48));
}

static struct page_info *p2m_alloc_page(struct domain *d) {
    struct page_info *pg;

    /*
     * For hardware domain, there should be no limit in the number of pages that
     * can be allocated, so that the kernel may take advantage of the extended
     * regions. Hence, allocate p2m pages for hardware domains from heap.
     */
    if (is_hardware_domain(d)) {
        pg = alloc_domheap_page(NULL, 0);
        if (pg == NULL) printk(PRTOSLOG_G_ERR "Failed to allocate P2M pages for hwdom.\n");
    } else {
        spin_lock(&d->arch.paging.lock);
        pg = page_list_remove_head(&d->arch.paging.p2m_freelist);
        spin_unlock(&d->arch.paging.lock);
    }

    return pg;
}

static void p2m_free_page(struct domain *d, struct page_info *pg) {
    if (is_hardware_domain(d))
        free_domheap_page(pg);
    else {
        spin_lock(&d->arch.paging.lock);
        page_list_add_tail(pg, &d->arch.paging.p2m_freelist);
        spin_unlock(&d->arch.paging.lock);
    }
}

/* Return the size of the pool, in bytes. */
int arch_get_paging_mempool_size(struct domain *d, uint64_t *size) {
    *size = (uint64_t)ACCESS_ONCE(d->arch.paging.p2m_total_pages) << PAGE_SHIFT;
    return 0;
}

/*
 * Set the pool of pages to the required number of pages.
 * Returns 0 for success, non-zero for failure.
 * Call with d->arch.paging.lock held.
 */
int p2m_set_allocation(struct domain *d, unsigned long pages, bool *preempted) {
    struct page_info *pg;

    ASSERT(spin_is_locked(&d->arch.paging.lock));

    for (;;) {
        if (d->arch.paging.p2m_total_pages < pages) {
            /* Need to allocate more memory from domheap */
            pg = alloc_domheap_page(NULL, 0);
            if (pg == NULL) {
                printk(PRTOSLOG_ERR "Failed to allocate P2M pages.\n");
                return -ENOMEM;
            }
            ACCESS_ONCE(d->arch.paging.p2m_total_pages) = d->arch.paging.p2m_total_pages + 1;
            page_list_add_tail(pg, &d->arch.paging.p2m_freelist);
        } else if (d->arch.paging.p2m_total_pages > pages) {
            /* Need to return memory to domheap */
            pg = page_list_remove_head(&d->arch.paging.p2m_freelist);
            if (pg) {
                ACCESS_ONCE(d->arch.paging.p2m_total_pages) = d->arch.paging.p2m_total_pages - 1;
                free_domheap_page(pg);
            } else {
                printk(PRTOSLOG_ERR "Failed to free P2M pages, P2M freelist is empty.\n");
                return -ENOMEM;
            }
        } else
            break;

        /* Check to see if we need to yield and try again */
        if (preempted && general_preempt_check()) {
            *preempted = true;
            return -ERESTART;
        }
    }

    return 0;
}

int arch_set_paging_mempool_size(struct domain *d, uint64_t size) {
    unsigned long pages = size >> PAGE_SHIFT;
    bool preempted = false;
    int rc;

    if ((size & ~PAGE_MASK) ||         /* Non page-sized request? */
        pages != (size >> PAGE_SHIFT)) /* 32-bit overflow? */
        return -EINVAL;

    spin_lock(&d->arch.paging.lock);
    rc = p2m_set_allocation(d, pages, &preempted);
    spin_unlock(&d->arch.paging.lock);

    ASSERT(preempted == (rc == -ERESTART));

    return rc;
}

int p2m_teardown_allocation(struct domain *d) {
    int ret = 0;
    bool preempted = false;

    spin_lock(&d->arch.paging.lock);
    if (d->arch.paging.p2m_total_pages != 0) {
        ret = p2m_set_allocation(d, 0, &preempted);
        if (preempted) {
            spin_unlock(&d->arch.paging.lock);
            return -ERESTART;
        }
        ASSERT(d->arch.paging.p2m_total_pages == 0);
    }
    spin_unlock(&d->arch.paging.lock);

    return ret;
}

void p2m_dump_info(struct domain *d) {
    struct p2m_domain *p2m = p2m_get_hostp2m(d);

    p2m_read_lock(p2m);
    printk("p2m mappings for domain %d (vmid %d):\n", d->domain_id, p2m->vmid);
    BUG_ON(p2m->stats.mappings[0] || p2m->stats.shattered[0]);
    printk("  1G mappings: %ld (shattered %ld)\n", p2m->stats.mappings[1], p2m->stats.shattered[1]);
    printk("  2M mappings: %ld (shattered %ld)\n", p2m->stats.mappings[2], p2m->stats.shattered[2]);
    printk("  4K mappings: %ld\n", p2m->stats.mappings[3]);
    p2m_read_unlock(p2m);
}


/*
 * p2m_save_state and p2m_restore_state work in pair to workaround
 * ARM64_WORKAROUND_AT_SPECULATE. p2m_save_state will set-up VTTBR to
 * point to the empty page-tables to stop allocating TLB entries.
 */
void p2m_save_state(struct vcpu *p) {
    p->arch.sctlr = READ_SYSREG(SCTLR_EL1);

    if (cpus_have_const_cap(ARM64_WORKAROUND_AT_SPECULATE)) {
        WRITE_SYSREG64(generate_vttbr(INVALID_VMID, empty_root_mfn), VTTBR_EL2);
        /*
         * Ensure VTTBR_EL2 is correctly synchronized so we can restore
         * the next vCPU context without worrying about AT instruction
         * speculation.
         */
        isb();
    }
}

void p2m_restore_state(struct vcpu *n) {
    struct p2m_domain *p2m = p2m_get_hostp2m(n->domain);
    uint8_t *last_vcpu_ran;

    if (is_idle_vcpu(n)) return;

    WRITE_SYSREG(n->arch.sctlr, SCTLR_EL1);
    WRITE_SYSREG(n->arch.hcr_el2, HCR_EL2);

    /*
     * ARM64_WORKAROUND_AT_SPECULATE: VTTBR_EL2 should be restored after all
     * registers associated to EL1/EL0 translations regime have been
     * synchronized.
     */
    asm volatile(ALTERNATIVE("nop", "isb", ARM64_WORKAROUND_AT_SPECULATE));
    WRITE_SYSREG64(p2m->vttbr, VTTBR_EL2);

    last_vcpu_ran = &p2m->last_vcpu_ran[smp_processor_id()];

    /*
     * While we are restoring an out-of-context translation regime
     * we still need to ensure:
     *  - VTTBR_EL2 is synchronized before flushing the TLBs
     *  - All registers for EL1 are synchronized before executing an AT
     *  instructions targeting S1/S2.
     */
    isb();

    /*
     * Flush local TLB for the domain to prevent wrong TLB translation
     * when running multiple vCPU of the same domain on a single pCPU.
     */
    if (*last_vcpu_ran != INVALID_VCPU_ID && *last_vcpu_ran != n->vcpu_id) flush_guest_tlb_local();

    *last_vcpu_ran = n->vcpu_id;
}

/*
 * Force a synchronous P2M TLB flush.
 *
 * Must be called with the p2m lock held.
 */
void p2m_force_tlb_flush_sync(struct p2m_domain *p2m) {
    unsigned long flags = 0;
    uint64_t ovttbr;

    ASSERT(p2m_is_write_locked(p2m));

    /*
     * ARM only provides an instruction to flush TLBs for the current
     * VMID. So switch to the VTTBR of a given P2M if different.
     */
    ovttbr = READ_SYSREG64(VTTBR_EL2);
    if (ovttbr != p2m->vttbr) {
        uint64_t vttbr;

        local_irq_save(flags);

        /*
         * ARM64_WORKAROUND_AT_SPECULATE: We need to stop AT to allocate
         * TLBs entries because the context is partially modified. We
         * only need the VMID for flushing the TLBs, so we can generate
         * a new VTTBR with the VMID to flush and the empty root table.
         */
        if (!cpus_have_const_cap(ARM64_WORKAROUND_AT_SPECULATE))
            vttbr = p2m->vttbr;
        else
            vttbr = generate_vttbr(p2m->vmid, empty_root_mfn);

        WRITE_SYSREG64(vttbr, VTTBR_EL2);

        /* Ensure VTTBR_EL2 is synchronized before flushing the TLBs */
        isb();
    }

    flush_guest_tlb();

    if (ovttbr != READ_SYSREG64(VTTBR_EL2)) {
        WRITE_SYSREG64(ovttbr, VTTBR_EL2);
        /* Ensure VTTBR_EL2 is back in place before continuing. */
        isb();
        local_irq_restore(flags);
    }

    p2m->need_flush = false;
}

void p2m_tlb_flush_sync(struct p2m_domain *p2m) {
    if (p2m->need_flush) p2m_force_tlb_flush_sync(p2m);
}

/*
 * Find and map the root page table. The caller is responsible for
 * unmapping the table.
 *
 * The function will return NULL if the offset of the root table is
 * invalid.
 */
static lpae_t *p2m_get_root_pointer(struct p2m_domain *p2m, gfn_t gfn) {
    unsigned long root_table;

    /*
     * While the root table index is the offset from the previous level,
     * we can't use (P2M_ROOT_LEVEL - 1) because the root level might be
     * 0. Yet we still want to check if all the unused bits are zeroed.
     */
    root_table = gfn_x(gfn) >> (PRTOS_PT_LEVEL_ORDER(P2M_ROOT_LEVEL) + PRTOS_PT_LPAE_SHIFT);
    if (root_table >= P2M_ROOT_PAGES) return NULL;

    return __map_domain_page(p2m->root + root_table);
}

/*
 * Lookup the MFN corresponding to a domain's GFN.
 * Lookup mem access in the ratrix tree.
 * The entries associated to the GFN is considered valid.
 */
static p2m_access_t p2m_mem_access_radix_get(struct p2m_domain *p2m, gfn_t gfn) {
    void *ptr;

    if (!p2m->mem_access_enabled) return p2m->default_access;

    ptr = radix_tree_lookup(&p2m->mem_access_settings, gfn_x(gfn));
    if (!ptr)
        return p2m_access_rwx;
    else
        return radix_tree_ptr_to_int(ptr);
}

/*
 * In the case of the P2M, the valid bit is used for other purpose. Use
 * the type to check whether an entry is valid.
 */
static inline bool p2m_is_valid(lpae_t pte) {
    return pte.p2m.type != p2m_invalid;
}

/*
 * lpae_is_* helpers don't check whether the valid bit is set in the
 * PTE. Provide our own overlay to check the valid bit.
 */
static inline bool p2m_is_mapping(lpae_t pte, unsigned int level) {
    return p2m_is_valid(pte) && lpae_is_mapping(pte, level);
}

static inline bool p2m_is_superpage(lpae_t pte, unsigned int level) {
    return p2m_is_valid(pte) && lpae_is_superpage(pte, level);
}

#define GUEST_TABLE_MAP_FAILED 0
#define GUEST_TABLE_SUPER_PAGE 1
#define GUEST_TABLE_NORMAL_PAGE 2

static int p2m_create_table(struct p2m_domain *p2m, lpae_t *entry);

/*
 * Take the currently mapped table, find the corresponding GFN entry,
 * and map the next table, if available. The previous table will be
 * unmapped if the next level was mapped (e.g GUEST_TABLE_NORMAL_PAGE
 * returned).
 *
 * The read_only parameters indicates whether intermediate tables should
 * be allocated when not present.
 *
 * Return values:
 *  GUEST_TABLE_MAP_FAILED: Either read_only was set and the entry
 *  was empty, or allocating a new page failed.
 *  GUEST_TABLE_NORMAL_PAGE: next level mapped normally
 *  GUEST_TABLE_SUPER_PAGE: The next entry points to a superpage.
 */
static int p2m_next_level(struct p2m_domain *p2m, bool read_only, unsigned int level, lpae_t **table, unsigned int offset) {
    lpae_t *entry;
    int ret;
    mfn_t mfn;

    entry = *table + offset;

    if (!p2m_is_valid(*entry)) {
        if (read_only) return GUEST_TABLE_MAP_FAILED;

        ret = p2m_create_table(p2m, entry);
        if (ret) return GUEST_TABLE_MAP_FAILED;
    }

    /* The function p2m_next_level is never called at the 3rd level */
    ASSERT(level < 3);
    if (p2m_is_mapping(*entry, level)) return GUEST_TABLE_SUPER_PAGE;

    mfn = lpae_get_mfn(*entry);

    unmap_domain_page(*table);
    *table = map_domain_page(mfn);

    return GUEST_TABLE_NORMAL_PAGE;
}

/*
 * Get the details of a given gfn.
 *
 * If the entry is present, the associated MFN will be returned and the
 * access and type filled up. The page_order will correspond to the
 * order of the mapping in the page table (i.e it could be a superpage).
 *
 * If the entry is not present, INVALID_MFN will be returned and the
 * page_order will be set according to the order of the invalid range.
 *
 * valid will contain the value of bit[0] (e.g valid bit) of the
 * entry.
 */
mfn_t p2m_get_entry(struct p2m_domain *p2m, gfn_t gfn, p2m_type_t *t, p2m_access_t *a, unsigned int *page_order, bool *valid) {
    paddr_t addr = gfn_to_gaddr(gfn);
    unsigned int level = 0;
    lpae_t entry, *table;
    int rc;
    mfn_t mfn = INVALID_MFN;
    p2m_type_t _t;
    DECLARE_OFFSETS(offsets, addr);

    ASSERT(p2m_is_locked(p2m));
    BUILD_BUG_ON(THIRD_MASK != PAGE_MASK);

    /* Allow t to be NULL */
    t = t ?: &_t;

    *t = p2m_invalid;

    if (valid) *valid = false;

    /* XXX: Check if the mapping is lower than the mapped gfn */

    /* This gfn is higher than the highest the p2m map currently holds */
    if (gfn_x(gfn) > gfn_x(p2m->max_mapped_gfn)) {
        for (level = P2M_ROOT_LEVEL; level < 3; level++)
            if ((gfn_x(gfn) & (PRTOS_PT_LEVEL_MASK(level) >> PAGE_SHIFT)) > gfn_x(p2m->max_mapped_gfn)) break;

        goto out;
    }

    table = p2m_get_root_pointer(p2m, gfn);

    /*
     * the table should always be non-NULL because the gfn is below
     * p2m->max_mapped_gfn and the root table pages are always present.
     */
    if (!table) {
        ASSERT_UNREACHABLE();
        level = P2M_ROOT_LEVEL;
        goto out;
    }

    for (level = P2M_ROOT_LEVEL; level < 3; level++) {
        rc = p2m_next_level(p2m, true, level, &table, offsets[level]);
        if (rc == GUEST_TABLE_MAP_FAILED)
            goto out_unmap;
        else if (rc != GUEST_TABLE_NORMAL_PAGE)
            break;
    }

    entry = table[offsets[level]];

    if (p2m_is_valid(entry)) {
        *t = entry.p2m.type;

        if (a) *a = p2m_mem_access_radix_get(p2m, gfn);

        mfn = lpae_get_mfn(entry);
        /*
         * The entry may point to a superpage. Find the MFN associated
         * to the GFN.
         */
        mfn = mfn_add(mfn, gfn_x(gfn) & ((1UL << PRTOS_PT_LEVEL_ORDER(level)) - 1));

        if (valid) *valid = lpae_is_valid(entry);
    }

out_unmap:
    unmap_domain_page(table);

out:
    if (page_order) *page_order = PRTOS_PT_LEVEL_ORDER(level);

    return mfn;
}

static void p2m_set_permission(lpae_t *e, p2m_type_t t, p2m_access_t a) {
    /* First apply type permissions */
    switch (t) {
        case p2m_ram_rw:
            e->p2m.xn = 0;
            e->p2m.write = 1;
            break;

        case p2m_ram_ro:
            e->p2m.xn = 0;
            e->p2m.write = 0;
            break;

        case p2m_iommu_map_rw:
        case p2m_map_foreign_rw:
        case p2m_grant_map_rw:
        case p2m_mmio_direct_dev:
        case p2m_mmio_direct_nc:
        case p2m_mmio_direct_c:
            e->p2m.xn = 1;
            e->p2m.write = 1;
            break;

        case p2m_iommu_map_ro:
        case p2m_map_foreign_ro:
        case p2m_grant_map_ro:
        case p2m_invalid:
            e->p2m.xn = 1;
            e->p2m.write = 0;
            break;

        case p2m_max_real_type:
            BUG();
            break;
    }

    /* Then restrict with access permissions */
    switch (a) {
        case p2m_access_rwx:
            break;
        case p2m_access_wx:
            e->p2m.read = 0;
            break;
        case p2m_access_rw:
            e->p2m.xn = 1;
            break;
        case p2m_access_w:
            e->p2m.read = 0;
            e->p2m.xn = 1;
            break;
        case p2m_access_rx:
        case p2m_access_rx2rw:
            e->p2m.write = 0;
            break;
        case p2m_access_x:
            e->p2m.write = 0;
            e->p2m.read = 0;
            break;
        case p2m_access_r:
            e->p2m.write = 0;
            e->p2m.xn = 1;
            break;
        case p2m_access_n:
        case p2m_access_n2rwx:
            e->p2m.read = e->p2m.write = 0;
            e->p2m.xn = 1;
            break;
    }
}

static lpae_t mfn_to_p2m_entry(mfn_t mfn, p2m_type_t t, p2m_access_t a) {
    /*
     * sh, xn and write bit will be defined in the following switches
     * based on mattr and t.
     */
    lpae_t e = (lpae_t){
        .p2m.af = 1,
        .p2m.read = 1,
        .p2m.table = 1,
        .p2m.valid = 1,
        .p2m.type = t,
    };

    BUILD_BUG_ON(p2m_max_real_type > (1 << 4));

    switch (t) {
        case p2m_mmio_direct_dev:
            e.p2m.mattr = MATTR_DEV;
            e.p2m.sh = LPAE_SH_OUTER;
            break;

        case p2m_mmio_direct_c:
            e.p2m.mattr = MATTR_MEM;
            e.p2m.sh = LPAE_SH_OUTER;
            break;

        /*
         * ARM ARM: Overlaying the shareability attribute (DDI
         * 0406C.b B3-1376 to 1377)
         *
         * A memory region with a resultant memory type attribute of Normal,
         * and a resultant cacheability attribute of Inner Non-cacheable,
         * Outer Non-cacheable, must have a resultant shareability attribute
         * of Outer Shareable, otherwise shareability is UNPREDICTABLE.
         *
         * On ARMv8 shareability is ignored and explicitly treated as Outer
         * Shareable for Normal Inner Non_cacheable, Outer Non-cacheable.
         * See the note for table D4-40, in page 1788 of the ARM DDI 0487A.j.
         */
        case p2m_mmio_direct_nc:
            e.p2m.mattr = MATTR_MEM_NC;
            e.p2m.sh = LPAE_SH_OUTER;
            break;

        default:
            e.p2m.mattr = MATTR_MEM;
            e.p2m.sh = LPAE_SH_INNER;
            break;
    }

    p2m_set_permission(&e, t, a);

    ASSERT(!(mfn_to_maddr(mfn) & ~PADDR_MASK));

    lpae_set_mfn(e, mfn);

    return e;
}

/* Generate table entry with correct attributes. */
static lpae_t page_to_p2m_table(struct page_info *page) {
    /*
     * The access value does not matter because the hardware will ignore
     * the permission fields for table entry.
     *
     * We use p2m_ram_rw so the entry has a valid type. This is important
     * for p2m_is_valid() to return valid on table entries.
     */
    return mfn_to_p2m_entry(page_to_mfn(page), p2m_ram_rw, p2m_access_rwx);
}

static inline void p2m_write_pte(lpae_t *p, lpae_t pte, bool clean_pte) {
    write_pte(p, pte);
    if (clean_pte) clean_dcache(*p);
}

static inline void p2m_remove_pte(lpae_t *p, bool clean_pte) {
    lpae_t pte;

    memset(&pte, 0x00, sizeof(pte));
    p2m_write_pte(p, pte, clean_pte);
}

/* Allocate a new page table page and hook it in via the given entry. */
static int p2m_create_table(struct p2m_domain *p2m, lpae_t *entry) {
    struct page_info *page;
    lpae_t *p;

    ASSERT(!p2m_is_valid(*entry));

    page = p2m_alloc_page(p2m->domain);
    if (page == NULL) return -ENOMEM;

    page_list_add(page, &p2m->pages);

    p = __map_domain_page(page);
    clear_page(p);

    if (p2m->clean_pte) clean_dcache_va_range(p, PAGE_SIZE);

    unmap_domain_page(p);

    p2m_write_pte(entry, page_to_p2m_table(page), p2m->clean_pte);

    return 0;
}

static int p2m_mem_access_radix_set(struct p2m_domain *p2m, gfn_t gfn, p2m_access_t a) {
    int rc;

    if (!p2m->mem_access_enabled) return 0;

    if (p2m_access_rwx == a) {
        radix_tree_delete(&p2m->mem_access_settings, gfn_x(gfn));
        return 0;
    }

    rc = radix_tree_insert(&p2m->mem_access_settings, gfn_x(gfn), radix_tree_int_to_ptr(a));
    if (rc == -EEXIST) {
        /* If a setting already exists, change it to the new one */
        radix_tree_replace_slot(radix_tree_lookup_slot(&p2m->mem_access_settings, gfn_x(gfn)), radix_tree_int_to_ptr(a));
        rc = 0;
    }

    return rc;
}

static void p2m_put_foreign_page(struct page_info *pg) {
    /*
     * It's safe to do the put_page here because page_alloc will
     * flush the TLBs if the page is reallocated before the end of
     * this loop.
     */
    put_page(pg);
}

/* Put any references on the single 4K page referenced by mfn. */
static void p2m_put_l3_page(mfn_t mfn, p2m_type_t type) {
    /* TODO: Handle other p2m types */
    if (p2m_is_foreign(type)) {
        ASSERT(mfn_valid(mfn));
        p2m_put_foreign_page(mfn_to_page(mfn));
    }
    /* Detect the prtosheap page and mark the stored GFN as invalid. */
    else if (p2m_is_ram(type) && is_prtos_heap_mfn(mfn))
        page_set_prtosheap_gfn(mfn_to_page(mfn), INVALID_GFN);
}

/* Put any references on the superpage referenced by mfn. */
static void p2m_put_l2_superpage(mfn_t mfn, p2m_type_t type) {
    struct page_info *pg;
    unsigned int i;

    /*
     * TODO: Handle other p2m types, but be aware that any changes to handle
     * different types should require an update on the relinquish code to handle
     * preemption.
     */
    if (!p2m_is_foreign(type)) return;

    ASSERT(mfn_valid(mfn));

    pg = mfn_to_page(mfn);

    for (i = 0; i < PRTOS_PT_LPAE_ENTRIES; i++, pg++) p2m_put_foreign_page(pg);
}

/* Put any references on the page referenced by pte. */
static void p2m_put_page(const lpae_t pte, unsigned int level) {
    mfn_t mfn = lpae_get_mfn(pte);

    ASSERT(p2m_is_valid(pte));

    /*
     * TODO: Currently we don't handle level 1 super-page, PRTOS is not
     * preemptible and therefore some work is needed to handle such
     * superpages, for which at some point PRTOS might end up freeing memory
     * and therefore for such a big mapping it could end up in a very long
     * operation.
     */
    if (level == 2)
        return p2m_put_l2_superpage(mfn, pte.p2m.type);
    else if (level == 3)
        return p2m_put_l3_page(mfn, pte.p2m.type);
}

/* Free lpae sub-tree behind an entry */
static void p2m_free_entry(struct p2m_domain *p2m, lpae_t entry, unsigned int level) {
    unsigned int i;
    lpae_t *table;
    mfn_t mfn;
    struct page_info *pg;

    /* Nothing to do if the entry is invalid. */
    if (!p2m_is_valid(entry)) return;

    if (p2m_is_superpage(entry, level) || (level == 3)) {
#ifdef CONFIG_IOREQ_SERVER
        /*
         * If this gets called then either the entry was replaced by an entry
         * with a different base (valid case) or the shattering of a superpage
         * has failed (error case).
         * So, at worst, the spurious mapcache invalidation might be sent.
         */
        if (p2m_is_ram(entry.p2m.type) && domain_has_ioreq_server(p2m->domain)) ioreq_request_mapcache_invalidate(p2m->domain);
#endif

        p2m->stats.mappings[level]--;

        p2m_put_page(entry, level);

        return;
    }

    table = map_domain_page(lpae_get_mfn(entry));
    for (i = 0; i < PRTOS_PT_LPAE_ENTRIES; i++) p2m_free_entry(p2m, *(table + i), level + 1);

    unmap_domain_page(table);

    /*
     * Make sure all the references in the TLB have been removed before
     * freing the intermediate page table.
     * XXX: Should we defer the free of the page table to avoid the
     * flush?
     */
    p2m_tlb_flush_sync(p2m);

    mfn = lpae_get_mfn(entry);
    ASSERT(mfn_valid(mfn));

    pg = mfn_to_page(mfn);

    page_list_del(pg, &p2m->pages);
    p2m_free_page(p2m->domain, pg);
}

static bool p2m_split_superpage(struct p2m_domain *p2m, lpae_t *entry, unsigned int level, unsigned int target, const unsigned int *offsets) {
    struct page_info *page;
    unsigned int i;
    lpae_t pte, *table;
    bool rv = true;

    /* Convenience aliases */
    mfn_t mfn = lpae_get_mfn(*entry);
    unsigned int next_level = level + 1;
    unsigned int level_order = PRTOS_PT_LEVEL_ORDER(next_level);

    /*
     * This should only be called with target != level and the entry is
     * a superpage.
     */
    ASSERT(level < target);
    ASSERT(p2m_is_superpage(*entry, level));

    page = p2m_alloc_page(p2m->domain);
    if (!page) return false;

    page_list_add(page, &p2m->pages);
    table = __map_domain_page(page);

    /*
     * We are either splitting a first level 1G page into 512 second level
     * 2M pages, or a second level 2M page into 512 third level 4K pages.
     */
    for (i = 0; i < PRTOS_PT_LPAE_ENTRIES; i++) {
        lpae_t *new_entry = table + i;

        /*
         * Use the content of the superpage entry and override
         * the necessary fields. So the correct permission are kept.
         */
        pte = *entry;
        lpae_set_mfn(pte, mfn_add(mfn, i << level_order));

        /*
         * First and second level pages set p2m.table = 0, but third
         * level entries set p2m.table = 1.
         */
        pte.p2m.table = (next_level == 3);

        write_pte(new_entry, pte);
    }

    /* Update stats */
    p2m->stats.shattered[level]++;
    p2m->stats.mappings[level]--;
    p2m->stats.mappings[next_level] += PRTOS_PT_LPAE_ENTRIES;

    /*
     * Shatter superpage in the page to the level we want to make the
     * changes.
     * This is done outside the loop to avoid checking the offset to
     * know whether the entry should be shattered for every entry.
     */
    if (next_level != target) rv = p2m_split_superpage(p2m, table + offsets[next_level], level + 1, target, offsets);

    if (p2m->clean_pte) clean_dcache_va_range(table, PAGE_SIZE);

    unmap_domain_page(table);

    /*
     * Even if we failed, we should install the newly allocated LPAE
     * entry. The caller will be in charge to free the sub-tree.
     */
    p2m_write_pte(entry, page_to_p2m_table(page), p2m->clean_pte);

    return rv;
}

/*
 * Insert an entry in the p2m. This should be called with a mapping
 * equal to a page/superpage (4K, 2M, 1G).
 */
static int __p2m_set_entry(struct p2m_domain *p2m, gfn_t sgfn, unsigned int page_order, mfn_t smfn, p2m_type_t t, p2m_access_t a) {
    unsigned int level = 0;
    unsigned int target = 3 - (page_order / PRTOS_PT_LPAE_SHIFT);
    lpae_t *entry, *table, orig_pte;
    int rc;
    /* A mapping is removed if the MFN is invalid. */
    bool removing_mapping = mfn_eq(smfn, INVALID_MFN);
    DECLARE_OFFSETS(offsets, gfn_to_gaddr(sgfn));

    ASSERT(p2m_is_write_locked(p2m));

    /*
     * Check if the level target is valid: we only support
     * 4K - 2M - 1G mapping.
     */
    ASSERT(target > 0 && target <= 3);

    table = p2m_get_root_pointer(p2m, sgfn);
    if (!table) return -EINVAL;

    for (level = P2M_ROOT_LEVEL; level < target; level++) {
        /*
         * Don't try to allocate intermediate page table if the mapping
         * is about to be removed.
         */
        rc = p2m_next_level(p2m, removing_mapping, level, &table, offsets[level]);
        if (rc == GUEST_TABLE_MAP_FAILED) {
            /*
             * We are here because p2m_next_level has failed to map
             * the intermediate page table (e.g the table does not exist
             * and they p2m tree is read-only). It is a valid case
             * when removing a mapping as it may not exist in the
             * page table. In this case, just ignore it.
             */
            rc = removing_mapping ? 0 : -ENOENT;
            goto out;
        } else if (rc != GUEST_TABLE_NORMAL_PAGE)
            break;
    }

    entry = table + offsets[level];

    /*
     * If we are here with level < target, we must be at a leaf node,
     * and we need to break up the superpage.
     */
    if (level < target) {
        /* We need to split the original page. */
        lpae_t split_pte = *entry;

        ASSERT(p2m_is_superpage(*entry, level));

        if (!p2m_split_superpage(p2m, &split_pte, level, target, offsets)) {
            /*
             * The current super-page is still in-place, so re-increment
             * the stats.
             */
            p2m->stats.mappings[level]++;

            /* Free the allocated sub-tree */
            p2m_free_entry(p2m, split_pte, level);

            rc = -ENOMEM;
            goto out;
        }

        /*
         * Follow the break-before-sequence to update the entry.
         * For more details see (D4.7.1 in ARM DDI 0487A.j).
         */
        p2m_remove_pte(entry, p2m->clean_pte);
        p2m_force_tlb_flush_sync(p2m);

        p2m_write_pte(entry, split_pte, p2m->clean_pte);

        /* then move to the level we want to make real changes */
        for (; level < target; level++) {
            rc = p2m_next_level(p2m, true, level, &table, offsets[level]);

            /*
             * The entry should be found and either be a table
             * or a superpage if level 3 is not targeted
             */
            ASSERT(rc == GUEST_TABLE_NORMAL_PAGE || (rc == GUEST_TABLE_SUPER_PAGE && target < 3));
        }

        entry = table + offsets[level];
    }

    /*
     * We should always be there with the correct level because
     * all the intermediate tables have been installed if necessary.
     */
    ASSERT(level == target);

    orig_pte = *entry;

    /*
     * The radix-tree can only work on 4KB. This is only used when
     * memaccess is enabled and during shutdown.
     */
    ASSERT(!p2m->mem_access_enabled || page_order == 0 || p2m->domain->is_dying);
    /*
     * The access type should always be p2m_access_rwx when the mapping
     * is removed.
     */
    ASSERT(!mfn_eq(INVALID_MFN, smfn) || (a == p2m_access_rwx));
    /*
     * Update the mem access permission before update the P2M. So we
     * don't have to revert the mapping if it has failed.
     */
    rc = p2m_mem_access_radix_set(p2m, sgfn, a);
    if (rc) goto out;

    /*
     * Always remove the entry in order to follow the break-before-make
     * sequence when updating the translation table (D4.7.1 in ARM DDI
     * 0487A.j).
     */
    if (lpae_is_valid(orig_pte) || removing_mapping) p2m_remove_pte(entry, p2m->clean_pte);

    if (removing_mapping) /* Flush can be deferred if the entry is removed */
        p2m->need_flush |= !!lpae_is_valid(orig_pte);
    else {
        lpae_t pte = mfn_to_p2m_entry(smfn, t, a);

        if (level < 3) pte.p2m.table = 0; /* Superpage entry */

        /*
         * It is necessary to flush the TLB before writing the new entry
         * to keep coherency when the previous entry was valid.
         *
         * Although, it could be defered when only the permissions are
         * changed (e.g in case of memaccess).
         */
        if (lpae_is_valid(orig_pte)) {
            if (likely(!p2m->mem_access_enabled) || P2M_CLEAR_PERM(pte) != P2M_CLEAR_PERM(orig_pte))
                p2m_force_tlb_flush_sync(p2m);
            else
                p2m->need_flush = true;
        } else if (!p2m_is_valid(orig_pte)) /* new mapping */
            p2m->stats.mappings[level]++;

        p2m_write_pte(entry, pte, p2m->clean_pte);

        p2m->max_mapped_gfn = gfn_max(p2m->max_mapped_gfn, gfn_add(sgfn, (1UL << page_order) - 1));
        p2m->lowest_mapped_gfn = gfn_min(p2m->lowest_mapped_gfn, sgfn);
    }

    if (is_iommu_enabled(p2m->domain) && (lpae_is_valid(orig_pte) || lpae_is_valid(*entry))) {
        unsigned int flush_flags = 0;

        if (lpae_is_valid(orig_pte)) flush_flags |= IOMMU_FLUSHF_modified;
        if (lpae_is_valid(*entry)) flush_flags |= IOMMU_FLUSHF_added;

        rc = iommu_iotlb_flush(p2m->domain, _dfn(gfn_x(sgfn)), 1UL << page_order, flush_flags);
    } else
        rc = 0;

    /*
     * Free the entry only if the original pte was valid and the base
     * is different (to avoid freeing when permission is changed).
     */
    if (p2m_is_valid(orig_pte) && !mfn_eq(lpae_get_mfn(*entry), lpae_get_mfn(orig_pte))) p2m_free_entry(p2m, orig_pte, level);

out:
    unmap_domain_page(table);

    return rc;
}

int p2m_set_entry(struct p2m_domain *p2m, gfn_t sgfn, unsigned long nr, mfn_t smfn, p2m_type_t t, p2m_access_t a) {
    int rc = 0;

    /*
     * Any reference taken by the P2M mappings (e.g. foreign mapping) will
     * be dropped in relinquish_p2m_mapping(). As the P2M will still
     * be accessible after, we need to prevent mapping to be added when the
     * domain is dying.
     */
    if (unlikely(p2m->domain->is_dying)) return -ENOMEM;

    while (nr) {
        unsigned long mask;
        unsigned long order;

        /*
         * Don't take into account the MFN when removing mapping (i.e
         * MFN_INVALID) to calculate the correct target order.
         *
         * XXX: Support superpage mappings if nr is not aligned to a
         * superpage size.
         */
        mask = !mfn_eq(smfn, INVALID_MFN) ? mfn_x(smfn) : 0;
        mask |= gfn_x(sgfn) | nr;

        /* Always map 4k by 4k when memaccess is enabled */
        if (unlikely(p2m->mem_access_enabled))
            order = THIRD_ORDER;
        else if (!(mask & ((1UL << FIRST_ORDER) - 1)))
            order = FIRST_ORDER;
        else if (!(mask & ((1UL << SECOND_ORDER) - 1)))
            order = SECOND_ORDER;
        else
            order = THIRD_ORDER;

        rc = __p2m_set_entry(p2m, sgfn, order, smfn, t, a);
        if (rc) break;

        sgfn = gfn_add(sgfn, (1 << order));
        if (!mfn_eq(smfn, INVALID_MFN)) smfn = mfn_add(smfn, (1 << order));

        nr -= (1 << order);
    }

    return rc;
}

/* Invalidate all entries in the table. The p2m should be write locked. */
static void p2m_invalidate_table(struct p2m_domain *p2m, mfn_t mfn) {
    lpae_t *table;
    unsigned int i;

    ASSERT(p2m_is_write_locked(p2m));

    table = map_domain_page(mfn);

    for (i = 0; i < PRTOS_PT_LPAE_ENTRIES; i++) {
        lpae_t pte = table[i];

        /*
         * Writing an entry can be expensive because it may involve
         * cleaning the cache. So avoid updating the entry if the valid
         * bit is already cleared.
         */
        if (!pte.p2m.valid) continue;

        pte.p2m.valid = 0;

        p2m_write_pte(&table[i], pte, p2m->clean_pte);
    }

    unmap_domain_page(table);

    p2m->need_flush = true;
}

/*
 * The domain will not be scheduled anymore, so in theory we should
 * not need to flush the TLBs. Do it for safety purpose.
 * Note that all the devices have already been de-assigned. So we don't
 * need to flush the IOMMU TLB here.
 */
void p2m_clear_root_pages(struct p2m_domain *p2m) {
    unsigned int i;

    p2m_write_lock(p2m);

    for (i = 0; i < P2M_ROOT_PAGES; i++) clear_and_clean_page(p2m->root + i);

    p2m_force_tlb_flush_sync(p2m);

    p2m_write_unlock(p2m);
}

/*
 * Invalidate all entries in the root page-tables. This is
 * useful to get fault on entry and do an action.
 *
 * p2m_invalid_root() should not be called when the P2M is shared with
 * the IOMMU because it will cause IOMMU fault.
 */
static void p2m_invalidate_root(struct p2m_domain *p2m) {
    unsigned int i;

    ASSERT(!iommu_use_hap_pt(p2m->domain));

    p2m_write_lock(p2m);

    for (i = 0; i < P2M_ROOT_LEVEL; i++) p2m_invalidate_table(p2m, page_to_mfn(p2m->root + i));

    p2m_write_unlock(p2m);
}

void p2m_domain_creation_finished(struct domain *d) {
    /*
     * To avoid flushing the whole guest RAM on the first Set/Way, we
     * invalidate the P2M to track what has been accessed.
     *
     * This is only turned when IOMMU is not used or the page-table are
     * not shared because bit[0] (e.g valid bit) unset will result
     * IOMMU fault that could be not fixed-up.
     */
    if (!iommu_use_hap_pt(d)) p2m_invalidate_root(p2m_get_hostp2m(d));
}

/*
 * Resolve any translation fault due to change in the p2m. This
 * includes break-before-make and valid bit cleared.
 */
bool p2m_resolve_translation_fault(struct domain *d, gfn_t gfn) {
    struct p2m_domain *p2m = p2m_get_hostp2m(d);
    unsigned int level = 0;
    bool resolved = false;
    lpae_t entry, *table;

    /*
     * PRTOS: The idle domain has no p2m tables (p2m_init is skipped for
     * idle domains in arch_domain_create). PRTOS manages stage-2 directly
     * via VTTBR_EL2, so PRTOS's p2m cannot resolve faults from PRTOS partitions.
     */
    if (is_idle_domain(d)) return false;

    /* Convenience aliases */
    DECLARE_OFFSETS(offsets, gfn_to_gaddr(gfn));

    p2m_write_lock(p2m);

    /* This gfn is higher than the highest the p2m map currently holds */
    if (gfn_x(gfn) > gfn_x(p2m->max_mapped_gfn)) goto out;

    table = p2m_get_root_pointer(p2m, gfn);
    /*
     * The table should always be non-NULL because the gfn is below
     * p2m->max_mapped_gfn and the root table pages are always present.
     */
    if (!table) {
        ASSERT_UNREACHABLE();
        goto out;
    }

    /*
     * Go down the page-tables until an entry has the valid bit unset or
     * a block/page entry has been hit.
     */
    for (level = P2M_ROOT_LEVEL; level <= 3; level++) {
        int rc;

        entry = table[offsets[level]];

        if (level == 3) break;

        /* Stop as soon as we hit an entry with the valid bit unset. */
        if (!lpae_is_valid(entry)) break;

        rc = p2m_next_level(p2m, true, level, &table, offsets[level]);
        if (rc == GUEST_TABLE_MAP_FAILED)
            goto out_unmap;
        else if (rc != GUEST_TABLE_NORMAL_PAGE)
            break;
    }

    /*
     * If the valid bit of the entry is set, it means someone was playing with
     * the Stage-2 page table. Nothing to do and mark the fault as resolved.
     */
    if (lpae_is_valid(entry)) {
        resolved = true;
        goto out_unmap;
    }

    /*
     * The valid bit is unset. If the entry is still not valid then the fault
     * cannot be resolved, exit and report it.
     */
    if (!p2m_is_valid(entry)) goto out_unmap;

    /*
     * Now we have an entry with valid bit unset, but still valid from
     * the P2M point of view.
     *
     * If an entry is pointing to a table, each entry of the table will
     * have there valid bit cleared. This allows a function to clear the
     * full p2m with just a couple of write. The valid bit will then be
     * propagated on the fault.
     * If an entry is pointing to a block/page, no work to do for now.
     */
    if (lpae_is_table(entry, level)) p2m_invalidate_table(p2m, lpae_get_mfn(entry));

    /*
     * Now that the work on the entry is done, set the valid bit to prevent
     * another fault on that entry.
     */
    resolved = true;
    entry.p2m.valid = 1;

    p2m_write_pte(table + offsets[level], entry, p2m->clean_pte);

    /*
     * No need to flush the TLBs as the modified entry had the valid bit
     * unset.
     */

out_unmap:
    unmap_domain_page(table);

out:
    p2m_write_unlock(p2m);

    return resolved;
}

static struct page_info *p2m_allocate_root(void) {
    struct page_info *page;
    unsigned int i;

    page = alloc_domheap_pages(NULL, P2M_ROOT_ORDER, 0);
    if (page == NULL) return NULL;

    /* Clear both first level pages */
    for (i = 0; i < P2M_ROOT_PAGES; i++) clear_and_clean_page(page + i);

    return page;
}

static int p2m_alloc_table(struct domain *d) {
    struct p2m_domain *p2m = p2m_get_hostp2m(d);

    p2m->root = p2m_allocate_root();
    if (!p2m->root) return -ENOMEM;

    p2m->vttbr = generate_vttbr(p2m->vmid, page_to_mfn(p2m->root));

    /*
     * Make sure that all TLBs corresponding to the new VMID are flushed
     * before using it
     */
    p2m_write_lock(p2m);
    p2m_force_tlb_flush_sync(p2m);
    p2m_write_unlock(p2m);

    return 0;
}

int p2m_teardown(struct domain *d) {
    struct p2m_domain *p2m = p2m_get_hostp2m(d);
    unsigned long count = 0;
    struct page_info *pg;
    int rc = 0;

    p2m_write_lock(p2m);

    while ((pg = page_list_remove_head(&p2m->pages))) {
        p2m_free_page(p2m->domain, pg);
        count++;
        /* Arbitrarily preempt every 512 iterations */
        if (!(count % 512) && hypercall_preempt_check()) {
            rc = -ERESTART;
            break;
        }
    }

    p2m_write_unlock(p2m);

    return rc;
}

void p2m_final_teardown(struct domain *d) {
    struct p2m_domain *p2m = p2m_get_hostp2m(d);

    /* p2m not actually initialized */
    if (!p2m->domain) return;

    /*
     * No need to call relinquish_p2m_mapping() here because
     * p2m_final_teardown() is called either after domain_relinquish_resources()
     * where relinquish_p2m_mapping() has been called.
     */

    ASSERT(page_list_empty(&p2m->pages));

    while (p2m_teardown_allocation(d) == -ERESTART) continue; /* No preemption support here */
    ASSERT(page_list_empty(&d->arch.paging.p2m_freelist));

    if (p2m->root) free_domheap_pages(p2m->root, P2M_ROOT_ORDER);

    p2m->root = NULL;

    p2m_free_vmid(d);

    radix_tree_destroy(&p2m->mem_access_settings, NULL);

    p2m->domain = NULL;
}

int p2m_init(struct domain *d) {
    struct p2m_domain *p2m = p2m_get_hostp2m(d);
    int rc;
    unsigned int cpu;

    rwlock_init(&p2m->lock);
    spin_lock_init(&d->arch.paging.lock);
    INIT_PAGE_LIST_HEAD(&p2m->pages);
    INIT_PAGE_LIST_HEAD(&d->arch.paging.p2m_freelist);

    p2m->vmid = INVALID_VMID;
    p2m->max_mapped_gfn = _gfn(0);
    p2m->lowest_mapped_gfn = _gfn(ULONG_MAX);

    p2m->default_access = p2m_access_rwx;
    p2m->mem_access_enabled = false;
    radix_tree_init(&p2m->mem_access_settings);

    /*
     * Some IOMMUs don't support coherent PT walk. When the p2m is
     * shared with the CPU, PRTOS has to make sure that the PT changes have
     * reached the memory
     */
    p2m->clean_pte = is_iommu_enabled(d) && !iommu_has_feature(d, IOMMU_FEAT_COHERENT_WALK);

    /*
     * Make sure that the type chosen to is able to store the an vCPU ID
     * between 0 and the maximum of virtual CPUS supported as long as
     * the INVALID_VCPU_ID.
     */
    BUILD_BUG_ON((1 << (sizeof(p2m->last_vcpu_ran[0]) * 8)) < MAX_VIRT_CPUS);
    BUILD_BUG_ON((1 << (sizeof(p2m->last_vcpu_ran[0]) * 8)) < INVALID_VCPU_ID);

    for_each_possible_cpu(cpu) p2m->last_vcpu_ran[cpu] = INVALID_VCPU_ID;

    /*
     * "Trivial" initialisation is now complete.  Set the backpointer so
     * p2m_teardown() and friends know to do something.
     */
    p2m->domain = d;

    rc = p2m_alloc_vmid(d);
    if (rc) return rc;

    rc = p2m_alloc_table(d);
    if (rc) return rc;

    return 0;
}

/*
 * The function will go through the p2m and remove page reference when it
 * is required. The mapping will be removed from the p2m.
 *
 * XXX: See whether the mapping can be left intact in the p2m.
 */
int relinquish_p2m_mapping(struct domain *d) {
    struct p2m_domain *p2m = p2m_get_hostp2m(d);
    unsigned long count = 0;
    p2m_type_t t;
    int rc = 0;
    unsigned int order;
    gfn_t start, end;

    BUG_ON(!d->is_dying);
    /* No mappings can be added in the P2M after the P2M lock is released. */
    p2m_write_lock(p2m);

    start = p2m->lowest_mapped_gfn;
    end = gfn_add(p2m->max_mapped_gfn, 1);

    for (; gfn_x(start) < gfn_x(end); start = gfn_next_boundary(start, order)) {
        mfn_t mfn = p2m_get_entry(p2m, start, &t, NULL, &order, NULL);

        count++;
        /*
         * Arbitrarily preempt every 512 iterations or when we have a level-2
         * foreign mapping.
         */
        if ((!(count % 512) || (p2m_is_foreign(t) && (order > PRTOS_PT_LEVEL_ORDER(2)))) && hypercall_preempt_check()) {
            rc = -ERESTART;
            break;
        }

        /*
         * p2m_set_entry will take care of removing reference on page
         * when it is necessary and removing the mapping in the p2m.
         */
        if (!mfn_eq(mfn, INVALID_MFN)) {
            /*
             * For valid mapping, the start will always be aligned as
             * entry will be removed whilst relinquishing.
             */
            rc = __p2m_set_entry(p2m, start, order, INVALID_MFN, p2m_invalid, p2m_access_rwx);
            if (unlikely(rc)) {
                printk(PRTOSLOG_G_ERR "Unable to remove mapping gfn=%#" PRI_gfn " order=%u from the p2m of domain %d\n", gfn_x(start), order, d->domain_id);
                break;
            }
        }
    }

    /*
     * Update lowest_mapped_gfn so on the next call we still start where
     * we stopped.
     */
    p2m->lowest_mapped_gfn = start;

    p2m_write_unlock(p2m);

    return rc;
}

/*
 * Clean & invalidate RAM associated to the guest vCPU.
 *
 * The function can only work with the current vCPU and should be called
 * with IRQ enabled as the vCPU could get preempted.
 */
void p2m_flush_vm(struct vcpu *v) {
    struct p2m_domain *p2m = p2m_get_hostp2m(v->domain);
    int rc;
    gfn_t start = _gfn(0);

    ASSERT(v == current);
    ASSERT(local_irq_is_enabled());
    ASSERT(v->arch.need_flush_to_ram);

    do {
        rc = p2m_cache_flush_range(v->domain, &start, _gfn(ULONG_MAX));
        if (rc == -ERESTART) do_softirq();
    } while (rc == -ERESTART);

    if (rc != 0) gprintk(PRTOSLOG_WARNING, "P2M has not been correctly cleaned (rc = %d)\n", rc);

    /*
     * Invalidate the p2m to track which page was modified by the guest
     * between call of p2m_flush_vm().
     */
    p2m_invalidate_root(p2m);

    v->arch.need_flush_to_ram = false;
}

/* VTCR value to be configured by all CPUs. Set only once by the boot CPU */
static register_t __read_mostly vtcr;

static void setup_virt_paging_one(void *data) {
    WRITE_SYSREG(vtcr, VTCR_EL2);

    /*
     * ARM64_WORKAROUND_AT_SPECULATE: We want to keep the TLBs free from
     * entries related to EL1/EL0 translation regime until a guest vCPU
     * is running. For that, we need to set-up VTTBR to point to an empty
     * page-table and turn on stage-2 translation. The TLB entries
     * associated with EL1/EL0 translation regime will also be flushed in case
     * an AT instruction was speculated before hand.
     */
    if (cpus_have_cap(ARM64_WORKAROUND_AT_SPECULATE)) {
        WRITE_SYSREG64(generate_vttbr(INVALID_VMID, empty_root_mfn), VTTBR_EL2);
        WRITE_SYSREG(READ_SYSREG(HCR_EL2) | HCR_VM, HCR_EL2);
        isb();

        flush_all_guests_tlb_local();
    }
}

void __init setup_virt_paging(void) {
    /* Setup Stage 2 address translation */
    register_t val = VTCR_RES1 | VTCR_SH0_IS | VTCR_ORGN0_WBWA | VTCR_IRGN0_WBWA;

    static const struct {
        unsigned int pabits;     /* Physical Address Size */
        unsigned int t0sz;       /* Desired T0SZ, minimum in comment */
        unsigned int root_order; /* Page order of the root of the p2m */
        unsigned int sl0;        /* Desired SL0, maximum in comment */
    } pa_range_info[] __initconst = {
    /* T0SZ minimum and SL0 maximum from ARM DDI 0487H.a Table D5-6 */
    /*      PA size, t0sz(min), root-order, sl0(max) */
#ifdef CONFIG_ARM_64
        [0] = {32, 32 /*32*/, 0, 1}, [1] = {36, 28 /*28*/, 0, 1}, [2] = {40, 24 /*24*/, 1, 1}, [3] = {42, 22 /*22*/, 3, 1},
        [4] = {44, 20 /*20*/, 0, 2}, [5] = {48, 16 /*16*/, 0, 2}, [6] = {52, 12 /*12*/, 4, 2}, [7] = {0} /* Invalid */
#else
        {32, 0 /*0*/, 0, 1}, {40, 24 /*24*/, 1, 1}
#endif
    };

    unsigned int i;
    unsigned int pa_range = 0x10; /* Larger than any possible value */

#ifdef CONFIG_ARM_32
    /*
     * Typecast pa_range_info[].t0sz into arm32 bit variant.
     *
     * VTCR.T0SZ is bits [3:0] and S(sign extension), bit[4] for arm322.
     * Thus, pa_range_info[].t0sz is translated to its arm32 variant using
     * struct bitfields.
     */
    struct {
        signed int val : 5;
    } t0sz_32;
#else
    /*
     * Restrict "p2m_ipa_bits" if needed. As P2M table is always configured
     * with IPA bits == PA bits, compare against "pabits".
     */
    if (pa_range_info[system_cpuinfo.mm64.pa_range].pabits < p2m_ipa_bits) p2m_ipa_bits = pa_range_info[system_cpuinfo.mm64.pa_range].pabits;

    /*
     * cpu info sanitization made sure we support 16bits VMID only if all
     * cores are supporting it.
     */
    if (system_cpuinfo.mm64.vmid_bits == MM64_VMID_16_BITS_SUPPORT) max_vmid = MAX_VMID_16_BIT;
#endif

    /* Choose suitable "pa_range" according to the resulted "p2m_ipa_bits". */
    for (i = 0; i < ARRAY_SIZE(pa_range_info); i++) {
        if (p2m_ipa_bits == pa_range_info[i].pabits) {
            pa_range = i;
            break;
        }
    }

    /* Check if we found the associated entry in the array */
    if (pa_range >= ARRAY_SIZE(pa_range_info) || !pa_range_info[pa_range].pabits) panic("%u-bit P2M is not supported\n", p2m_ipa_bits);

#ifdef CONFIG_ARM_64
    val |= VTCR_PS(pa_range);
    val |= VTCR_TG0_4K;

    /* Set the VS bit only if 16 bit VMID is supported. */
    if (MAX_VMID == MAX_VMID_16_BIT) val |= VTCR_VS;
#endif

    val |= VTCR_SL0(pa_range_info[pa_range].sl0);
    val |= VTCR_T0SZ(pa_range_info[pa_range].t0sz);

    p2m_root_order = pa_range_info[pa_range].root_order;
    p2m_root_level = 2 - pa_range_info[pa_range].sl0;

#ifdef CONFIG_ARM_64
    p2m_ipa_bits = 64 - pa_range_info[pa_range].t0sz;
#else
    t0sz_32.val = pa_range_info[pa_range].t0sz;
    p2m_ipa_bits = 32 - t0sz_32.val;
#endif

    printk("P2M: %d-bit IPA with %d-bit PA and %d-bit VMID\n", p2m_ipa_bits, pa_range_info[pa_range].pabits, (MAX_VMID == MAX_VMID_16_BIT) ? 16 : 8);

    printk("P2M: %d levels with order-%d root, VTCR 0x%" PRIregister "\n", 4 - P2M_ROOT_LEVEL, P2M_ROOT_ORDER, val);

    p2m_vmid_allocator_init();

    /* It is not allowed to concatenate a level zero root */
    BUG_ON(P2M_ROOT_LEVEL == 0 && P2M_ROOT_ORDER > 0);
    vtcr = val;

    /*
     * ARM64_WORKAROUND_AT_SPECULATE requires to allocate root table
     * with all entries zeroed.
     */
    if (cpus_have_cap(ARM64_WORKAROUND_AT_SPECULATE)) {
        struct page_info *root;

        root = p2m_allocate_root();
        if (!root) panic("Unable to allocate root table for ARM64_WORKAROUND_AT_SPECULATE\n");

        empty_root_mfn = page_to_mfn(root);
    }

    setup_virt_paging_one(NULL);
    smp_call_function(setup_virt_paging_one, NULL, 1);
}

static int cpu_virt_paging_callback(struct notifier_block *nfb, unsigned long action, void *hcpu) {
    switch (action) {
        case CPU_STARTING:
            ASSERT(system_state != SYS_STATE_boot);
            setup_virt_paging_one(NULL);
            break;
        default:
            break;
    }

    return NOTIFY_DONE;
}

static struct notifier_block cpu_virt_paging_nfb = {
    .notifier_call = cpu_virt_paging_callback,
};

static int __init cpu_virt_paging_init(void) {
    register_cpu_notifier(&cpu_virt_paging_nfb);

    return 0;
}
/*
 * Initialization of the notifier has to be done at init rather than presmp_init
 * phase because: the registered notifier is used to setup virtual paging for
 * non-boot CPUs after the initial virtual paging for all CPUs is already setup,
 * i.e. when a non-boot CPU is hotplugged after the system has booted. In other
 * words, the notifier should be registered after the virtual paging is
 * initially setup (setup_virt_paging() is called from start_prtos()). This is
 * required because vtcr config value has to be set before a notifier can fire.
 */
__initcall(cpu_virt_paging_init);

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_mmu_p2m.c === */
/* === BEGIN INLINED: arch_arm_mmu_setup.c === */
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/mmu/setup.c
 *
 * MMU system boot CPU MM bringup code.
 */

#include <prtos_prtos_config.h>

#include <prtos_init.h>
#include <prtos_libfdt_libfdt.h>
#include <prtos_sections.h>
#include <prtos_sizes.h>
#include <prtos_vmap.h>

#include <asm_setup.h>
#include <asm_fixmap.h>

/* Override macros from asm/page.h to make them work with mfn_t */
#undef mfn_to_virt
#define mfn_to_virt(mfn) __mfn_to_virt(mfn_x(mfn))

/* Main runtime page tables */

/*
 * For arm32 prtos_pgtable are per-PCPU and are allocated before
 * bringing up each CPU. For arm64 prtos_pgtable is common to all PCPUs.
 *
 * prtos_second, prtos_fixmap and prtos_map are always shared between all
 * PCPUs.
 */

#ifdef CONFIG_ARM_64
DEFINE_PAGE_TABLE(prtos_pgtable);
static DEFINE_PAGE_TABLE(prtos_first);
#define THIS_CPU_PGTABLE prtos_pgtable
#else
#endif

/* Common pagetable leaves */
/* Second level page table used to cover PRTOS virtual address space */
static DEFINE_PAGE_TABLE(prtos_second);
/* Third level page table used for fixmap */
DEFINE_BOOT_PAGE_TABLE(prtos_fixmap);
/*
 * Third level page table used to map PRTOS itself with the XN bit set
 * as appropriate.
 */
static DEFINE_PAGE_TABLES(prtos_map, PRTOS_NR_ENTRIES(2));

static paddr_t phys_offset;

/* Limits of the PRTOS heap */
mfn_t directmap_mfn_start __read_mostly = INVALID_MFN_INITIALIZER;
mfn_t directmap_mfn_end __read_mostly;
vaddr_t directmap_virt_end __read_mostly;
#ifdef CONFIG_ARM_64
vaddr_t directmap_virt_start __read_mostly;
unsigned long directmap_base_pdx __read_mostly;
#endif

/* Checking VA memory layout alignment. */
static void __init __maybe_unused build_assertions(void)
{
    /* 2MB aligned regions */
    BUILD_BUG_ON(PRTOS_VIRT_START & ~SECOND_MASK);
    BUILD_BUG_ON(FIXMAP_ADDR(0) & ~SECOND_MASK);
    /* 1GB aligned regions */
#ifdef CONFIG_ARM_32
    BUILD_BUG_ON(PRTOSHEAP_VIRT_START & ~FIRST_MASK);
#else
    BUILD_BUG_ON(DIRECTMAP_VIRT_START & ~FIRST_MASK);
#endif
    /* Page table structure constraints */
#ifdef CONFIG_ARM_64
    /*
     * The first few slots of the L0 table is reserved for the identity
     * mapping. Check that none of the other regions are overlapping
     * with it.
     */
#define CHECK_OVERLAP_WITH_IDMAP(virt) \
    BUILD_BUG_ON(zeroeth_table_offset(virt) < IDENTITY_MAPPING_AREA_NR_L0)

    CHECK_OVERLAP_WITH_IDMAP(PRTOS_VIRT_START);
    CHECK_OVERLAP_WITH_IDMAP(VMAP_VIRT_START);
    CHECK_OVERLAP_WITH_IDMAP(FRAMETABLE_VIRT_START);
    CHECK_OVERLAP_WITH_IDMAP(DIRECTMAP_VIRT_START);
#undef CHECK_OVERLAP_WITH_IDMAP
#endif
    BUILD_BUG_ON(first_table_offset(PRTOS_VIRT_START));
#ifdef CONFIG_ARCH_MAP_DOMAIN_PAGE
    BUILD_BUG_ON(DOMHEAP_VIRT_START & ~FIRST_MASK);
#endif
    /*
     * The boot code expects the regions PRTOS_VIRT_START, FIXMAP_ADDR(0),
     * BOOT_FDT_VIRT_START to use the same 0th (arm64 only) and 1st
     * slot in the page tables.
     */
#define CHECK_SAME_SLOT(level, virt1, virt2) \
    BUILD_BUG_ON(level##_table_offset(virt1) != level##_table_offset(virt2))

#define CHECK_DIFFERENT_SLOT(level, virt1, virt2) \
    BUILD_BUG_ON(level##_table_offset(virt1) == level##_table_offset(virt2))

#ifdef CONFIG_ARM_64
    CHECK_SAME_SLOT(zeroeth, PRTOS_VIRT_START, FIXMAP_ADDR(0));
    CHECK_SAME_SLOT(zeroeth, PRTOS_VIRT_START, BOOT_FDT_VIRT_START);
#endif
    CHECK_SAME_SLOT(first, PRTOS_VIRT_START, FIXMAP_ADDR(0));
    CHECK_SAME_SLOT(first, PRTOS_VIRT_START, BOOT_FDT_VIRT_START);

    /*
     * For arm32, the temporary mapping will re-use the domheap
     * first slot and the second slots will match.
     */
#ifdef CONFIG_ARM_32

#endif

#undef CHECK_SAME_SLOT
#undef CHECK_DIFFERENT_SLOT
}

lpae_t __init pte_of_prtosaddr(vaddr_t va)
{
    paddr_t ma = va + phys_offset;

    return mfn_to_prtos_entry(maddr_to_mfn(ma), MT_NORMAL);
}

void * __init early_fdt_map(paddr_t fdt_paddr)
{
    /* We are using 2MB superpage for mapping the FDT */
    paddr_t base_paddr = fdt_paddr & SECOND_MASK;
    paddr_t offset = 0;
    void *fdt_virt = (void *)(unsigned long)fdt_paddr;
    uint32_t size;
    int rc;

    // /*
    //  * Check whether the physical FDT address is set and meets the minimum
    //  * alignment requirement. Since we are relying on MIN_FDT_ALIGN to be at
    //  * least 8 bytes so that we always access the magic and size fields
    //  * of the FDT header after mapping the first chunk, double check if
    //  * that is indeed the case.
    //  */
    // BUILD_BUG_ON(MIN_FDT_ALIGN < 8);
    // if ( !fdt_paddr || fdt_paddr % MIN_FDT_ALIGN )
    //     return NULL;

    // /* The FDT is mapped using 2MB superpage */
    // BUILD_BUG_ON(BOOT_FDT_VIRT_START % SZ_2M);

    // rc = map_pages_to_prtos(BOOT_FDT_VIRT_START, maddr_to_mfn(base_paddr),
    //                       SZ_2M >> PAGE_SHIFT,
    //                       PAGE_HYPERVISOR_RO | _PAGE_BLOCK);
    // if ( rc )
    //     panic("Unable to map the device-tree.\n");


    // offset = fdt_paddr % SECOND_SIZE;
    // fdt_virt = (void *)BOOT_FDT_VIRT_START + offset;

    if ( fdt_magic(fdt_virt) != FDT_MAGIC )
        return NULL;

    size = fdt_totalsize(fdt_virt);
    if ( size > MAX_FDT_SIZE )
        return NULL;

    if ( (offset + size) > SZ_2M )
    {
        rc = map_pages_to_prtos(BOOT_FDT_VIRT_START + SZ_2M,
                              maddr_to_mfn(base_paddr + SZ_2M),
                              SZ_2M >> PAGE_SHIFT,
                              PAGE_HYPERVISOR_RO | _PAGE_BLOCK);
        if ( rc )
            panic("Unable to map the device-tree\n");
    }

    return fdt_virt;
}

void __init remove_early_mappings(void)
{
    int rc;

    /* destroy the _PAGE_BLOCK mapping */
    rc = modify_prtos_mappings(BOOT_FDT_VIRT_START,
                             BOOT_FDT_VIRT_START + BOOT_FDT_VIRT_SIZE,
                             _PAGE_BLOCK);
    BUG_ON(rc);
}

/*
 * After boot, PRTOS page-tables should not contain mapping that are both
 * Writable and eXecutables.
 *
 * This should be called on each CPU to enforce the policy.
 */
static void prtos_pt_enforce_wnx(void)
{
    // WRITE_SYSREG(READ_SYSREG(SCTLR_EL2) | SCTLR_Axx_ELx_WXN, SCTLR_EL2); // TODO: enforce_wnx is not implemented yet for PRTOS
    /*
     * The TLBs may cache SCTLR_EL2.WXN. So ensure it is synchronized
     * before flushing the TLBs.
     */
    isb();
    flush_prtos_tlb_local();
}

/*
 * Boot-time pagetable setup.
 * Changes here may need matching changes in head.S
 */
void __init setup_pagetables(unsigned long boot_phys_offset)
{
    uint64_t ttbr;
    lpae_t pte, *p;
    int i;

    phys_offset = boot_phys_offset;
    printk("""boot_phys_offset: 0x%lx\n", boot_phys_offset); // Debugging line to print the boot_phys_offset with PRTOS printk API for demonstration purposes

    arch_setup_page_tables();

#ifdef CONFIG_ARM_64
    pte = pte_of_prtosaddr((uintptr_t)prtos_first);
    pte.pt.table = 1;
    pte.pt.xn = 0;
    prtos_pgtable[zeroeth_table_offset(PRTOS_VIRT_START)] = pte;

    p = (void *) prtos_first;
#else
#endif

    /* Map prtos second level page-table */
    p[0] = pte_of_prtosaddr((uintptr_t)(prtos_second));
    p[0].pt.table = 1;
    p[0].pt.xn = 0;

    /* Break up the PRTOS mapping into pages and protect them separately. */
    for ( i = 0; i < PRTOS_NR_ENTRIES(3); i++ )
    {
        vaddr_t va = PRTOS_VIRT_START + (i << PAGE_SHIFT);

        // if ( !is_kernel(va) )
        //     break;
        pte = pte_of_prtosaddr(va);
        pte.pt.table = 1; /* third level mappings always have this bit set */
        pte.pt.xn = 0;

        // TODO: enforce_wnx is not implemented yet for PRTOS, so comment the next two if code blocks
        if ( is_kernel_text(va) || is_kernel_inittext(va) ) {// Just make the prtos kernel text read-only
             /* Kernel text is read-only, so set the RO bit */
             // pte.pt.xn = 0;
             pte.pt.ro = 1;
        }
        if ( is_kernel_rodata(va) )
             pte.pt.ro = 1;
        prtos_map[i] = pte;
    }

    /* Initialise prtos second level entries ... */
    /* ... PRTOS's text etc */
    for ( i = 0; i < PRTOS_NR_ENTRIES(2); i++ )
    {
        vaddr_t va = PRTOS_VIRT_START + (i << PRTOS_PT_LEVEL_SHIFT(2));

        pte = pte_of_prtosaddr((vaddr_t)(prtos_map + i * PRTOS_PT_LPAE_ENTRIES));
        pte.pt.table = 1;
        prtos_second[second_table_offset(va)] = pte;
    }

    /* ... Fixmap */
    pte = pte_of_prtosaddr((vaddr_t)prtos_fixmap);
    pte.pt.table = 1;
    prtos_second[second_table_offset(FIXMAP_ADDR(0))] = pte;

#ifdef CONFIG_ARM_64
    ttbr = (uintptr_t) prtos_pgtable + phys_offset;
#else
#endif

    switch_ttbr(ttbr);

    prtos_pt_enforce_wnx();

#ifdef CONFIG_ARM_32
#endif
}

void *__init arch_vmap_virt_end(void)
{
    return (void *)(VMAP_VIRT_START + VMAP_VIRT_SIZE);
}

/* Release all __init and __initdata ranges to be reused */
void free_init_memory(void)
{
//     paddr_t pa = virt_to_maddr(__init_begin);
//     unsigned long len = __init_end - __init_begin;
//     uint32_t insn;
//     unsigned int i, nr = len / sizeof(insn);
//     uint32_t *p;
//     int rc;

//     rc = modify_prtos_mappings((unsigned long)__init_begin,
//                              (unsigned long)__init_end, PAGE_HYPERVISOR_RW);
//     if ( rc )
//         panic("Unable to map RW the init section (rc = %d)\n", rc);

//     /*
//      * From now on, init will not be used for execution anymore,
//      * so nuke the instruction cache to remove entries related to init.
//      */
//     invalidate_icache_local();

// #ifdef CONFIG_ARM_32
//     /* udf instruction i.e (see A8.8.247 in ARM DDI 0406C.c) */
//     insn = 0xe7f000f0;
// #else
//     insn = AARCH64_BREAK_FAULT;
// #endif
//     p = (uint32_t *)__init_begin;
//     for ( i = 0; i < nr; i++ )
//         *(p + i) = insn;

//     rc = destroy_prtos_mappings((unsigned long)__init_begin,
//                               (unsigned long)__init_end);
//     if ( rc )
//         panic("Unable to remove the init section (rc = %d)\n", rc);

//     init_domheap_pages(pa, pa + len);
//     printk("Freed %ldkB init memory.\n", (long)(__init_end-__init_begin)>>10);
    printk("Fake msg: Freed %ldkB init memory.\n", 0L); // Debugging line to print the freed init memory size with PRTOS printk API for demonstration purposes
}

/**
 * copy_from_paddr - copy data from a physical address
 * @dst: destination virtual address
 * @paddr: source physical address
 * @len: length to copy
 */
void __init copy_from_paddr(void *dst, paddr_t paddr, unsigned long len)
{
    void *src = (void *)FIXMAP_ADDR(FIX_MISC);

    while (len) {
        unsigned long l, s;

        s = paddr & (PAGE_SIZE - 1);
        l = min(PAGE_SIZE - s, len);

        set_fixmap(FIX_MISC, maddr_to_mfn(paddr), PAGE_HYPERVISOR_WC);
        memcpy(dst, src + s, l);
        clean_dcache_va_range(dst, l);
        clear_fixmap(FIX_MISC);

        paddr += l;
        dst += l;
        len -= l;
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_mmu_setup.c === */
/* === BEGIN INLINED: arch_arm_mmu_smpboot.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/mmu/smpboot.c
 *
 * MMU system secondary CPUs MM bringup code.
 */

#include <prtos_domain_page.h>

#include <asm_setup.h>

/* Override macros from asm/page.h to make them work with mfn_t */
#undef virt_to_mfn
#define virt_to_mfn(va) _mfn(__virt_to_mfn(va))

/*
 * Static start-of-day pagetables that we use before the allocators
 * are up. These are used by all CPUs during bringup before switching
 * to the CPUs own pagetables.
 *
 * These pagetables have a very simple structure. They include:
 *  - PRTOS_VIRT_SIZE worth of L3 mappings of prtos at PRTOS_VIRT_START, boot_first
 *    and boot_second are used to populate the tables down to boot_third
 *    which contains the actual mapping.
 *  - a 1:1 mapping of prtos at its current physical address. This uses a
 *    section mapping at whichever of boot_{pgtable,first,second}
 *    covers that physical address.
 *
 * For the boot CPU these mappings point to the address where PRTOS was
 * loaded by the bootloader. For secondary CPUs they point to the
 * relocated copy of PRTOS for the benefit of secondary CPUs.
 *
 * In addition to the above for the boot CPU the device-tree is
 * initially mapped in the boot misc slot. This mapping is not present
 * for secondary CPUs.
 *
 * Finally, if EARLY_PRINTK is enabled then prtos_fixmap will be mapped
 * by the CPU once it has moved off the 1:1 mapping.
 */
DEFINE_BOOT_PAGE_TABLE(boot_pgtable);
#ifdef CONFIG_ARM_64
DEFINE_BOOT_PAGE_TABLE(boot_first);
DEFINE_BOOT_PAGE_TABLE(boot_first_id);
#endif
DEFINE_BOOT_PAGE_TABLE(boot_second_id);
DEFINE_BOOT_PAGE_TABLE(boot_third_id);
DEFINE_BOOT_PAGE_TABLE(boot_second);
DEFINE_BOOT_PAGE_TABLES(boot_third, PRTOS_NR_ENTRIES(2));

/* Non-boot CPUs use this to find the correct pagetables. */
uint64_t __section(".data.idmap") init_ttbr;

static void set_init_ttbr(lpae_t *root)
{
    /*
     * init_ttbr is part of the identity mapping which is read-only. So
     * we need to re-map the region so it can be updated.
     */
    void *ptr = map_domain_page(virt_to_mfn(&init_ttbr));

    ptr += PAGE_OFFSET(&init_ttbr);

    *(uint64_t *)ptr = virt_to_maddr(root);

    /*
     * init_ttbr will be accessed with the MMU off, so ensure the update
     * is visible by cleaning the cache.
     */
    clean_dcache_va_range(ptr, sizeof(uint64_t));

    unmap_domain_page(ptr);
}

#ifdef CONFIG_ARM_64
int prepare_secondary_mm(int cpu)
{
    // clear_boot_pagetables(); // commented out to avoid clearing the boot pagetables for PRTOS to kick cpu

    /*
     * Set init_ttbr for this CPU coming up. All CPUs share a single setof
     * pagetables, but rewrite it each time for consistency with 32 bit.
     */
    set_init_ttbr(prtos_pgtable);

    return 0;
}
#else

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_mmu_smpboot.c === */
/* === BEGIN INLINED: p2m.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
#include <prtos_iocap.h>
#include <prtos_lib.h>
#include <prtos_sched.h>
#include <prtos_softirq.h>

#include <asm_event.h>
#include <asm_flushtlb.h>
#include <asm_guest_walk.h>
#include <asm_mem_access.h>
#include <asm_page.h>
#include <asm_traps.h>

#ifdef CONFIG_ARM_64
unsigned int __read_mostly max_vmid = MAX_VMID_8_BIT;
#endif

/*
 * Set to the maximum configured support for IPA bits, so the number of IPA bits can be
 * restricted by external entity (e.g. IOMMU).
 */
unsigned int __read_mostly p2m_ipa_bits = PADDR_BITS;

/* Unlock the flush and do a P2M TLB flush if necessary */
void p2m_write_unlock(struct p2m_domain *p2m)
{
    /*
     * The final flush is done with the P2M write lock taken to avoid
     * someone else modifying the P2M wbefore the TLB invalidation has
     * completed.
     */
    p2m_tlb_flush_sync(p2m);

    write_unlock(&p2m->lock);
}

void memory_type_changed(struct domain *d)
{
}

mfn_t p2m_lookup(struct domain *d, gfn_t gfn, p2m_type_t *t)
{
    mfn_t mfn;
    struct p2m_domain *p2m = p2m_get_hostp2m(d);

    p2m_read_lock(p2m);
    mfn = p2m_get_entry(p2m, gfn, t, NULL, NULL, NULL);
    p2m_read_unlock(p2m);

    return mfn;
}

struct page_info *p2m_get_page_from_gfn(struct domain *d, gfn_t gfn,
                                        p2m_type_t *t)
{
    struct page_info *page;
    p2m_type_t p2mt;
    mfn_t mfn = p2m_lookup(d, gfn, &p2mt);

    if ( t )
        *t = p2mt;

    if ( !p2m_is_any_ram(p2mt) )
        return NULL;

    if ( !mfn_valid(mfn) )
        return NULL;

    page = mfn_to_page(mfn);

    /*
     * get_page won't work on foreign mapping because the page doesn't
     * belong to the current domain.
     */
    if ( p2m_is_foreign(p2mt) )
    {
        struct domain *fdom = page_get_owner_and_reference(page);
        ASSERT(fdom != NULL);
        ASSERT(fdom != d);
        return page;
    }

    return get_page(page, d) ? page : NULL;
}

int guest_physmap_mark_populate_on_demand(struct domain *d,
                                          unsigned long gfn,
                                          unsigned int order)
{
    return -ENOSYS;
}

unsigned long p2m_pod_decrease_reservation(struct domain *d, gfn_t gfn,
                                           unsigned int order)
{
    return 0;
}

int p2m_insert_mapping(struct domain *d, gfn_t start_gfn, unsigned long nr,
                       mfn_t mfn, p2m_type_t t)
{
    struct p2m_domain *p2m = p2m_get_hostp2m(d);
    int rc;

    p2m_write_lock(p2m);
    rc = p2m_set_entry(p2m, start_gfn, nr, mfn, t, p2m->default_access);
    p2m_write_unlock(p2m);

    return rc;
}

static inline int p2m_remove_mapping(struct domain *d,
                                     gfn_t start_gfn,
                                     unsigned long nr,
                                     mfn_t mfn)
{
    struct p2m_domain *p2m = p2m_get_hostp2m(d);
    unsigned long i;
    int rc;

    p2m_write_lock(p2m);
    /*
     * Before removing the GFN - MFN mapping for any RAM pages make sure
     * that there is no difference between what is already mapped and what
     * is requested to be unmapped.
     * If they don't match bail out early. For instance, this could happen
     * if two CPUs are requesting to unmap the same P2M entry concurrently.
     */
    for ( i = 0; i < nr; )
    {
        unsigned int cur_order;
        p2m_type_t t;
        mfn_t mfn_return = p2m_get_entry(p2m, gfn_add(start_gfn, i), &t, NULL,
                                         &cur_order, NULL);

        if ( p2m_is_any_ram(t) &&
             (!mfn_valid(mfn) || !mfn_eq(mfn_add(mfn, i), mfn_return)) )
        {
            rc = -EILSEQ;
            goto out;
        }

        i += (1UL << cur_order) -
             ((gfn_x(start_gfn) + i) & ((1UL << cur_order) - 1));
    }

    rc = p2m_set_entry(p2m, start_gfn, nr, INVALID_MFN,
                       p2m_invalid, p2m_access_rwx);

out:
    p2m_write_unlock(p2m);

    return rc;
}

int map_regions_p2mt(struct domain *d,
                     gfn_t gfn,
                     unsigned long nr,
                     mfn_t mfn,
                     p2m_type_t p2mt)
{
    return p2m_insert_mapping(d, gfn, nr, mfn, p2mt);
}


int map_mmio_regions(struct domain *d,
                     gfn_t start_gfn,
                     unsigned long nr,
                     mfn_t mfn)
{
    return p2m_insert_mapping(d, start_gfn, nr, mfn, p2m_mmio_direct_dev);
}

int unmap_mmio_regions(struct domain *d,
                       gfn_t start_gfn,
                       unsigned long nr,
                       mfn_t mfn)
{
    return p2m_remove_mapping(d, start_gfn, nr, mfn);
}

int map_dev_mmio_page(struct domain *d, gfn_t gfn, mfn_t mfn)
{
    int res;

    if ( !iomem_access_permitted(d, mfn_x(mfn), mfn_x(mfn)) )
        return 0;

    res = p2m_insert_mapping(d, gfn, 1, mfn, p2m_mmio_direct_c);
    if ( res < 0 )
    {
        printk(PRTOSLOG_G_ERR "Unable to map MFN %#"PRI_mfn" in %pd\n",
               mfn_x(mfn), d);
        return res;
    }

    return 0;
}

int guest_physmap_add_entry(struct domain *d,
                            gfn_t gfn,
                            mfn_t mfn,
                            unsigned long page_order,
                            p2m_type_t t)
{
    return p2m_insert_mapping(d, gfn, (1 << page_order), mfn, t);
}

int guest_physmap_remove_page(struct domain *d, gfn_t gfn, mfn_t mfn,
                              unsigned int page_order)
{
    return p2m_remove_mapping(d, gfn, (1 << page_order), mfn);
}

int set_foreign_p2m_entry(struct domain *d, const struct domain *fd,
                          unsigned long gfn, mfn_t mfn)
{
    struct page_info *page = mfn_to_page(mfn);
    int rc;

    ASSERT(arch_acquire_resource_check(d));

    if ( !get_page(page, fd) )
        return -EINVAL;

    /*
     * It is valid to always use p2m_map_foreign_rw here as if this gets
     * called then d != fd. A case when d == fd would be rejected by
     * rcu_lock_remote_domain_by_id() earlier. Put a respective ASSERT()
     * to catch incorrect usage in future.
     */
    ASSERT(d != fd);

    rc = guest_physmap_add_entry(d, _gfn(gfn), mfn, 0, p2m_map_foreign_rw);
    if ( rc )
        put_page(page);

    return rc;
}

static spinlock_t vmid_alloc_lock = SPIN_LOCK_UNLOCKED;

/*
 * VTTBR_EL2 VMID field is 8 or 16 bits. AArch64 may support 16-bit VMID.
 * Using a bitmap here limits us to 256 or 65536 (for AArch64) concurrent
 * domains. The bitmap space will be allocated dynamically based on
 * whether 8 or 16 bit VMIDs are supported.
 */
static unsigned long *vmid_mask;

void p2m_vmid_allocator_init(void)
{
    /*
     * allocate space for vmid_mask based on MAX_VMID
     */
    vmid_mask = xzalloc_array(unsigned long, BITS_TO_LONGS(MAX_VMID));

    if ( !vmid_mask )
        panic("Could not allocate VMID bitmap space\n");

    set_bit(INVALID_VMID, vmid_mask);
}

int p2m_alloc_vmid(struct domain *d)
{
    struct p2m_domain *p2m = p2m_get_hostp2m(d);

    int rc, nr;

    spin_lock(&vmid_alloc_lock);

    nr = find_first_zero_bit(vmid_mask, MAX_VMID);

    ASSERT(nr != INVALID_VMID);

    if ( nr == MAX_VMID )
    {
        rc = -EBUSY;
        printk(PRTOSLOG_ERR "p2m.c: dom%d: VMID pool exhausted\n", d->domain_id);
        goto out;
    }

    set_bit(nr, vmid_mask);

    p2m->vmid = nr;

    rc = 0;

out:
    spin_unlock(&vmid_alloc_lock);
    return rc;
}

void p2m_free_vmid(struct domain *d)
{
    struct p2m_domain *p2m = p2m_get_hostp2m(d);
    spin_lock(&vmid_alloc_lock);
    if ( p2m->vmid != INVALID_VMID )
        clear_bit(p2m->vmid, vmid_mask);

    spin_unlock(&vmid_alloc_lock);
}

int p2m_cache_flush_range(struct domain *d, gfn_t *pstart, gfn_t end)
{
    struct p2m_domain *p2m = p2m_get_hostp2m(d);
    gfn_t next_block_gfn;
    gfn_t start = *pstart;
    mfn_t mfn = INVALID_MFN;
    p2m_type_t t;
    unsigned int order;
    int rc = 0;
    /* Counter for preemption */
    unsigned short count = 0;

    /*
     * The operation cache flush will invalidate the RAM assigned to the
     * guest in a given range. It will not modify the page table and
     * flushing the cache whilst the page is used by another CPU is
     * fine. So using read-lock is fine here.
     */
    p2m_read_lock(p2m);

    start = gfn_max(start, p2m->lowest_mapped_gfn);
    end = gfn_min(end, gfn_add(p2m->max_mapped_gfn, 1));

    next_block_gfn = start;

    while ( gfn_x(start) < gfn_x(end) )
    {
       /*
         * Cleaning the cache for the P2M may take a long time. So we
         * need to be able to preempt. We will arbitrarily preempt every
         * time count reach 512 or above.
         *
         * The count will be incremented by:
         *  - 1 on region skipped
         *  - 10 for each page requiring a flush
         */
        if ( count >= 512 )
        {
            if ( softirq_pending(smp_processor_id()) )
            {
                rc = -ERESTART;
                break;
            }
            count = 0;
        }

        /*
         * We want to flush page by page as:
         *  - it may not be possible to map the full block (can be up to 1GB)
         *    in PRTOS memory
         *  - we may want to do fine grain preemption as flushing multiple
         *    page in one go may take a long time
         *
         * As p2m_get_entry is able to return the size of the mapping
         * in the p2m, it is pointless to execute it for each page.
         *
         * We can optimize it by tracking the gfn of the next
         * block. So we will only call p2m_get_entry for each block (can
         * be up to 1GB).
         */
        if ( gfn_eq(start, next_block_gfn) )
        {
            bool valid;

            mfn = p2m_get_entry(p2m, start, &t, NULL, &order, &valid);
            next_block_gfn = gfn_next_boundary(start, order);

            if ( mfn_eq(mfn, INVALID_MFN) || !p2m_is_any_ram(t) || !valid )
            {
                count++;
                start = next_block_gfn;
                continue;
            }
        }

        count += 10;

        flush_page_to_ram(mfn_x(mfn), false);

        start = gfn_add(start, 1);
        mfn = mfn_add(mfn, 1);
    }

    if ( rc != -ERESTART )
        invalidate_icache();

    p2m_read_unlock(p2m);

    *pstart = start;

    return rc;
}

/*
 * See note at ARMv7 ARM B1.14.4 (DDI 0406C.c) (TL;DR: S/W ops are not
 * easily virtualized).
 *
 * Main problems:
 *  - S/W ops are local to a CPU (not broadcast)
 *  - We have line migration behind our back (speculation)
 *  - System caches don't support S/W at all (damn!)
 *
 * In the face of the above, the best we can do is to try and convert
 * S/W ops to VA ops. Because the guest is not allowed to infer the S/W
 * to PA mapping, it can only use S/W to nuke the whole cache, which is
 * rather a good thing for us.
 *
 * Also, it is only used when turning caches on/off ("The expected
 * usage of the cache maintenance instructions that operate by set/way
 * is associated with the powerdown and powerup of caches, if this is
 * required by the implementation.").
 *
 * We use the following policy:
 *  - If we trap a S/W operation, we enabled VM trapping to detect
 *  caches being turned on/off, and do a full clean.
 *
 *  - We flush the caches on both caches being turned on and off.
 *
 *  - Once the caches are enabled, we stop trapping VM ops.
 */
void p2m_set_way_flush(struct vcpu *v, struct cpu_user_regs *regs,
                       const union hsr hsr)
{
    /* This function can only work with the current vCPU. */
    ASSERT(v == current);

    if ( iommu_use_hap_pt(current->domain) )
    {
        gprintk(PRTOSLOG_ERR,
                "The cache should be flushed by VA rather than by set/way.\n");
        inject_undef_exception(regs, hsr);
        return;
    }

    if ( !(v->arch.hcr_el2 & HCR_TVM) )
    {
        v->arch.need_flush_to_ram = true;
        vcpu_hcr_set_flags(v, HCR_TVM);
    }
}

void p2m_toggle_cache(struct vcpu *v, bool was_enabled)
{
    bool now_enabled = vcpu_has_cache_enabled(v);

    /* This function can only work with the current vCPU. */
    ASSERT(v == current);

    /*
     * If switching the MMU+caches on, need to invalidate the caches.
     * If switching it off, need to clean the caches.
     * Clean + invalidate does the trick always.
     */
    if ( was_enabled != now_enabled )
        v->arch.need_flush_to_ram = true;

    /* Caches are now on, stop trapping VM ops (until a S/W op) */
    if ( now_enabled )
        vcpu_hcr_clear_flags(v, HCR_TVM);
}

mfn_t gfn_to_mfn(struct domain *d, gfn_t gfn)
{
    return p2m_lookup(d, gfn, NULL);
}

struct page_info *get_page_from_gva(struct vcpu *v, vaddr_t va,
                                    unsigned long flags)
{
    struct domain *d = v->domain;
    struct p2m_domain *p2m = p2m_get_hostp2m(d);
    struct page_info *page = NULL;
    paddr_t maddr = 0;
    uint64_t par;
    mfn_t mfn;
    p2m_type_t t;

    /*
     * XXX: To support a different vCPU, we would need to load the
     * VTTBR_EL2, TTBR0_EL1, TTBR1_EL1 and SCTLR_EL1
     */
    if ( v != current )
        return NULL;

    /*
     * The lock is here to protect us against the break-before-make
     * sequence used when updating the entry.
     */
    p2m_read_lock(p2m);
    par = gvirt_to_maddr(va, &maddr, flags);
    p2m_read_unlock(p2m);

    /*
     * gvirt_to_maddr may fail if the entry does not have the valid bit
     * set. Fallback to the second method:
     *  1) Translate the VA to IPA using software lookup -> Stage-1 page-table
     *  may not be accessible because the stage-2 entries may have valid
     *  bit unset.
     *  2) Software lookup of the MFN
     *
     * Note that when memaccess is enabled, we instead call directly
     * p2m_mem_access_check_and_get_page(...). Because the function is a
     * a variant of the methods described above, it will be able to
     * handle entries with valid bit unset.
     *
     * TODO: Integrate more nicely memaccess with the rest of the
     * function.
     * TODO: Use the fault error in PAR_EL1 to avoid pointless
     *  translation.
     */
    if ( par )
    {
        paddr_t ipa;
        unsigned int s1_perms;

        /*
         * When memaccess is enabled, the translation GVA to MADDR may
         * have failed because of a permission fault.
         */
        if ( p2m->mem_access_enabled )
            return p2m_mem_access_check_and_get_page(va, flags, v);

        /*
         * The software stage-1 table walk can still fail, e.g, if the
         * GVA is not mapped.
         */
        if ( !guest_walk_tables(v, va, &ipa, &s1_perms) )
        {
            dprintk(PRTOSLOG_G_DEBUG,
                    "%pv: Failed to walk page-table va %#"PRIvaddr"\n", v, va);
            return NULL;
        }

        mfn = p2m_lookup(d, gaddr_to_gfn(ipa), &t);
        if ( mfn_eq(INVALID_MFN, mfn) || !p2m_is_ram(t) )
            return NULL;

        /*
         * Check permission that are assumed by the caller. For instance
         * in case of guestcopy, the caller assumes that the translated
         * page can be accessed with the requested permissions. If this
         * is not the case, we should fail.
         *
         * Please note that we do not check for the GV2M_EXEC
         * permission. This is fine because the hardware-based translation
         * instruction does not test for execute permissions.
         */
        if ( (flags & GV2M_WRITE) && !(s1_perms & GV2M_WRITE) )
            return NULL;

        if ( (flags & GV2M_WRITE) && t != p2m_ram_rw )
            return NULL;
    }
    else
        mfn = maddr_to_mfn(maddr);

    if ( !mfn_valid(mfn) )
    {
        dprintk(PRTOSLOG_G_DEBUG, "%pv: Invalid MFN %#"PRI_mfn"\n",
                v, mfn_x(mfn));
        return NULL;
    }

    page = mfn_to_page(mfn);
    ASSERT(page);

    if ( unlikely(!get_page(page, d)) )
    {
        dprintk(PRTOSLOG_G_DEBUG, "%pv: Failing to acquire the MFN %#"PRI_mfn"\n",
                v, mfn_x(maddr_to_mfn(maddr)));
        return NULL;
    }

    return page;
}


/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: p2m.c === */
/* === BEGIN INLINED: mm.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/mm.c
 *
 * MMU code for an ARMv7-A with virt extensions.
 *
 * Tim Deegan <tim@xen.org>
 * Copyright (c) 2011 Citrix Systems.
 */

#include <prtos_domain_page.h>
#include <prtos_grant_table.h>
#include <prtos_guest_access.h>
#include <prtos_mm.h>

#include <prtos_xsm_xsm.h>

#include <public_memory.h>

/* Override macros from asm/page.h to make them work with mfn_t */
#undef virt_to_mfn
#define virt_to_mfn(va) _mfn(__virt_to_mfn(va))

unsigned long frametable_base_pdx __read_mostly;
unsigned long frametable_virt_end __read_mostly;

void flush_page_to_ram(unsigned long mfn, bool sync_icache)
{
    void *v = map_domain_page(_mfn(mfn));

    clean_and_invalidate_dcache_va_range(v, PAGE_SIZE);
    unmap_domain_page(v);

    /*
     * For some of the instruction cache (such as VIPT), the entire I-Cache
     * needs to be flushed to guarantee that all the aliases of a given
     * physical address will be removed from the cache.
     * Invalidating the I-Cache by VA highly depends on the behavior of the
     * I-Cache (See D4.9.2 in ARM DDI 0487A.k_iss10775). Instead of using flush
     * by VA on select platforms, we just flush the entire cache here.
     */
    if ( sync_icache )
        invalidate_icache();
}

/* Map a frame table to cover physical addresses ps through pe */
void __init setup_frametable_mappings(paddr_t ps, paddr_t pe)
{
    unsigned long nr_pdxs = mfn_to_pdx(mfn_add(maddr_to_mfn(pe), -1)) -
                            mfn_to_pdx(maddr_to_mfn(ps)) + 1;
    unsigned long frametable_size = nr_pdxs * sizeof(struct page_info);
    mfn_t base_mfn;
    const unsigned long mapping_size = frametable_size < MB(32) ? MB(2) : MB(32);
    int rc;

    /*
     * The size of paddr_t should be sufficient for the complete range of
     * physical address.
     */
    BUILD_BUG_ON((sizeof(paddr_t) * BITS_PER_BYTE) < PADDR_BITS);
    BUILD_BUG_ON(sizeof(struct page_info) != PAGE_INFO_SIZE);

    if ( frametable_size > FRAMETABLE_SIZE )
        panic("The frametable cannot cover the physical region %#"PRIpaddr" - %#"PRIpaddr"\n",
              ps, pe);

    frametable_base_pdx = mfn_to_pdx(maddr_to_mfn(ps));
    /* Round up to 2M or 32M boundary, as appropriate. */
    frametable_size = ROUNDUP(frametable_size, mapping_size);
    base_mfn = alloc_boot_pages(frametable_size >> PAGE_SHIFT, 32<<(20-12));

    rc = map_pages_to_prtos(FRAMETABLE_VIRT_START, base_mfn,
                          frametable_size >> PAGE_SHIFT,
                          PAGE_HYPERVISOR_RW | _PAGE_BLOCK);
    if ( rc )
        panic("Unable to setup the frametable mappings.\n");

    memset(&frame_table[0], 0, nr_pdxs * sizeof(struct page_info));
    memset(&frame_table[nr_pdxs], -1,
           frametable_size - (nr_pdxs * sizeof(struct page_info)));

    frametable_virt_end = FRAMETABLE_VIRT_START + (nr_pdxs * sizeof(struct page_info));
}

int steal_page(
    struct domain *d, struct page_info *page, unsigned int memflags)
{
    return -EOPNOTSUPP;
}

int page_is_ram_type(unsigned long mfn, unsigned long mem_type)
{
    ASSERT_UNREACHABLE();
    return 0;
}

unsigned long domain_get_maximum_gpfn(struct domain *d)
{
    return gfn_x(d->arch.p2m.max_mapped_gfn);
}

void share_prtos_page_with_guest(struct page_info *page, struct domain *d,
                               enum PRTOSSHARE_flags flags)
{
    if ( page_get_owner(page) == d )
        return;

    nrspin_lock(&d->page_alloc_lock);

    /*
     * The incremented type count pins as writable or read-only.
     *
     * Please note, the update of type_info field here is not atomic as
     * we use Read-Modify-Write operation on it. But currently it is fine
     * because the caller of page_set_prtosheap_gfn() (which is another place
     * where type_info is updated) would need to acquire a reference on
     * the page. This is only possible after the count_info is updated *and*
     * there is a barrier between the type_info and count_info. So there is
     * no immediate need to use cmpxchg() here.
     */
    page->u.inuse.type_info &= ~(PGT_type_mask | PGT_count_mask);
    page->u.inuse.type_info |= (flags == SHARE_ro ? PGT_none
                                                  : PGT_writable_page) |
                                MASK_INSR(1, PGT_count_mask);

    page_set_owner(page, d);
    smp_wmb(); /* install valid domain ptr before updating refcnt. */
    ASSERT((page->count_info & ~PGC_prtos_heap) == 0);

    /* Only add to the allocation list if the domain isn't dying. */
    if ( !d->is_dying )
    {
        page->count_info |= PGC_allocated | 1;
        if ( unlikely(d->prtosheap_pages++ == 0) )
            get_knownalive_domain(d);
        page_list_add_tail(page, &d->prtospage_list);
    }

    nrspin_unlock(&d->page_alloc_lock);
}

int prtosmem_add_to_physmap_one(
    struct domain *d,
    unsigned int space,
    union add_to_physmap_extra extra,
    unsigned long idx,
    gfn_t gfn)
{
    mfn_t mfn = INVALID_MFN;
    int rc;
    p2m_type_t t;
    struct page_info *page = NULL;

    switch ( space )
    {
    case PRTOSMAPSPACE_grant_table:
        rc = gnttab_map_frame(d, idx, gfn, &mfn);
        if ( rc )
            return rc;

        /* Need to take care of the reference obtained in gnttab_map_frame(). */
        page = mfn_to_page(mfn);
        t = p2m_ram_rw;

        break;
    case PRTOSMAPSPACE_shared_info:
        if ( idx != 0 )
            return -EINVAL;

        mfn = virt_to_mfn(d->shared_info);
        t = p2m_ram_rw;

        break;
    case PRTOSMAPSPACE_gmfn_foreign:
    {
        struct domain *od;
        p2m_type_t p2mt;

        od = get_pg_owner(extra.foreign_domid);
        if ( od == NULL )
            return -ESRCH;

        if ( od == d )
        {
            put_pg_owner(od);
            return -EINVAL;
        }

        rc = xsm_map_gmfn_foreign(XSM_TARGET, d, od);
        if ( rc )
        {
            put_pg_owner(od);
            return rc;
        }

        /* Take reference to the foreign domain page.
         * Reference will be released in PRTOSMEM_remove_from_physmap */
        page = get_page_from_gfn(od, idx, &p2mt, P2M_ALLOC);
        if ( !page )
        {
            put_pg_owner(od);
            return -EINVAL;
        }

        if ( p2m_is_ram(p2mt) )
            t = (p2mt == p2m_ram_rw) ? p2m_map_foreign_rw : p2m_map_foreign_ro;
        else
        {
            put_page(page);
            put_pg_owner(od);
            return -EINVAL;
        }

        mfn = page_to_mfn(page);

        put_pg_owner(od);
        break;
    }
    case PRTOSMAPSPACE_dev_mmio:
        rc = map_dev_mmio_page(d, gfn, _mfn(idx));
        return rc;

    default:
        return -ENOSYS;
    }

    /*
     * Map at new location. Here we need to map prtosheap RAM page differently
     * because we need to store the valid GFN and make sure that nothing was
     * mapped before (the stored GFN is invalid). And these actions need to be
     * performed with the P2M lock held. The guest_physmap_add_entry() is just
     * a wrapper on top of p2m_set_entry().
     */
    if ( !p2m_is_ram(t) || !is_prtos_heap_mfn(mfn) )
        rc = guest_physmap_add_entry(d, gfn, mfn, 0, t);
    else
    {
        struct p2m_domain *p2m = p2m_get_hostp2m(d);

        p2m_write_lock(p2m);
        if ( gfn_eq(page_get_prtosheap_gfn(mfn_to_page(mfn)), INVALID_GFN) )
        {
            rc = p2m_set_entry(p2m, gfn, 1, mfn, t, p2m->default_access);
            if ( !rc )
                page_set_prtosheap_gfn(mfn_to_page(mfn), gfn);
        }
        else
            /*
             * Mandate the caller to first unmap the page before mapping it
             * again. This is to prevent PRTOS creating an unwanted hole in
             * the P2M. For instance, this could happen if the firmware stole
             * a RAM address for mapping the shared_info page into but forgot
             * to unmap it afterwards.
             */
            rc = -EBUSY;
        p2m_write_unlock(p2m);
    }

    /*
     * For PRTOSMAPSPACE_gmfn_foreign if we failed to add the mapping, we need
     * to drop the reference we took earlier. In all other cases we need to
     * drop any reference we took earlier (perhaps indirectly).
     */
    if ( space == PRTOSMAPSPACE_gmfn_foreign ? rc : page != NULL )
    {
        ASSERT(page != NULL);
        put_page(page);
    }

    return rc;
}

long arch_memory_op(int op, PRTOS_GUEST_HANDLE_PARAM(void) arg)
{
    switch ( op )
    {
    /* XXX: memsharing not working yet */
    case PRTOSMEM_get_sharing_shared_pages:
    case PRTOSMEM_get_sharing_freed_pages:
        return 0;

    default:
        return -ENOSYS;
    }

    ASSERT_UNREACHABLE();
    return 0;
}

static struct domain *page_get_owner_and_nr_reference(struct page_info *page,
                                                      unsigned long nr)
{
    unsigned long x, y = page->count_info;
    struct domain *owner;

    /* Restrict nr to avoid "double" overflow */
    if ( nr >= PGC_count_mask )
    {
        ASSERT_UNREACHABLE();
        return NULL;
    }

    do {
        x = y;
        /*
         * Count ==  0: Page is not allocated, so we cannot take a reference.
         * Count == -1: Reference count would wrap, which is invalid.
         */
        if ( unlikely(((x + nr) & PGC_count_mask) <= nr) )
            return NULL;
    }
    while ( (y = cmpxchg(&page->count_info, x, x + nr)) != x );

    owner = page_get_owner(page);
    ASSERT(owner);

    return owner;
}

struct domain *page_get_owner_and_reference(struct page_info *page)
{
    return page_get_owner_and_nr_reference(page, 1);
}

void put_page_nr(struct page_info *page, unsigned long nr)
{
    unsigned long nx, x, y = page->count_info;

    do {
        ASSERT((y & PGC_count_mask) >= nr);
        x  = y;
        nx = x - nr;
    }
    while ( unlikely((y = cmpxchg(&page->count_info, x, nx)) != x) );

    if ( unlikely((nx & PGC_count_mask) == 0) )
    {
        if ( unlikely(nx & PGC_static) )
            free_domstatic_page(page);
        else
            free_domheap_page(page);
    }
}

void put_page(struct page_info *page)
{
    put_page_nr(page, 1);
}

bool get_page_nr(struct page_info *page, const struct domain *domain,
                 unsigned long nr)
{
    const struct domain *owner = page_get_owner_and_nr_reference(page, nr);

    if ( likely(owner == domain) )
        return true;

    if ( owner != NULL )
        put_page_nr(page, nr);

    return false;
}

bool get_page(struct page_info *page, const struct domain *domain)
{
    return get_page_nr(page, domain, 1);
}

/* Common code requires get_page_type and put_page_type.
 * We don't care about typecounts so we just do the minimum to make it
 * happy. */
int get_page_type(struct page_info *page, unsigned long type)
{
    return 1;
}

void put_page_type(struct page_info *page)
{
    return;
}

int create_grant_host_mapping(uint64_t gpaddr, mfn_t frame,
                              unsigned int flags, unsigned int cache_flags)
{
    int rc;
    p2m_type_t t = p2m_grant_map_rw;

    if ( cache_flags  || (flags & ~GNTMAP_readonly) != GNTMAP_host_map )
        return GNTST_general_error;

    if ( flags & GNTMAP_readonly )
        t = p2m_grant_map_ro;

    rc = guest_physmap_add_entry(current->domain, gaddr_to_gfn(gpaddr),
                                 frame, 0, t);

    if ( rc )
        return GNTST_general_error;
    else
        return GNTST_okay;
}

int replace_grant_host_mapping(uint64_t gpaddr, mfn_t frame,
                               uint64_t new_gpaddr, unsigned int flags)
{
    gfn_t gfn = gaddr_to_gfn(gpaddr);
    struct domain *d = current->domain;
    int rc;

    if ( new_gpaddr != 0 || (flags & GNTMAP_contains_pte) )
        return GNTST_general_error;

    rc = guest_physmap_remove_page(d, gfn, frame, 0);

    return rc ? GNTST_general_error : GNTST_okay;
}

bool is_iomem_page(mfn_t mfn)
{
    return !mfn_valid(mfn);
}

void clear_and_clean_page(struct page_info *page)
{
    void *p = __map_domain_page(page);

    clear_page(p);
    clean_dcache_va_range(p, PAGE_SIZE);
    unmap_domain_page(p);
}

unsigned long get_upper_mfn_bound(void)
{
    /* No memory hotplug yet, so current memory limit is the final one. */
    return max_page - 1;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: mm.c === */
/* === BEGIN INLINED: memory.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * memory.c
 *
 * Code to handle memory-related requests.
 *
 * Copyright (c) 2003-2004, B Dragovic
 * Copyright (c) 2003-2005, K A Fraser
 */

#include <prtos_domain_page.h>
#include <prtos_errno.h>
#include <prtos_event.h>
#include <prtos_grant_table.h>
#include <prtos_guest_access.h>
#include <prtos_hypercall.h>
#include <prtos_iocap.h>
#include <prtos_ioreq.h>
#include <prtos_lib.h>
#include <prtos_mem_access.h>
#include <prtos_mm.h>
#include <prtos_numa.h>
#include <prtos_paging.h>
#include <prtos_param.h>
#include <prtos_perfc.h>
#include <prtos_sched.h>
#include <prtos_trace.h>
#include <prtos_types.h>
#include <asm_current.h>
#include <asm_generic_hardirq.h>
#include <asm_p2m.h>
#include <asm_page.h>
#include <public_memory.h>
#include <prtos_xsm_xsm.h>

#ifdef CONFIG_X86
#include <asm/guest.h>
#endif

struct memop_args {
    /* INPUT */
    struct domain *domain;     /* Domain to be affected. */
    PRTOS_GUEST_HANDLE(prtos_pfn_t) extent_list; /* List of extent base addrs. */
    unsigned int nr_extents;   /* Number of extents to allocate or free. */
    unsigned int extent_order; /* Size of each extent. */
    unsigned int memflags;     /* Allocation flags. */

    /* INPUT/OUTPUT */
    unsigned int nr_done;    /* Number of extents processed so far. */
    int          preempted;  /* Was the hypercall preempted? */
};

#ifndef CONFIG_CTLDOM_MAX_ORDER
#define CONFIG_CTLDOM_MAX_ORDER CONFIG_PAGEALLOC_MAX_ORDER
#endif
#ifndef CONFIG_PTDOM_MAX_ORDER
#define CONFIG_PTDOM_MAX_ORDER CONFIG_HWDOM_MAX_ORDER
#endif

static unsigned int __read_mostly domu_max_order = CONFIG_DOMU_MAX_ORDER;
static unsigned int __read_mostly ctldom_max_order = CONFIG_CTLDOM_MAX_ORDER;
static unsigned int __read_mostly hwdom_max_order = CONFIG_HWDOM_MAX_ORDER;
#ifdef CONFIG_HAS_PASSTHROUGH
static unsigned int __read_mostly ptdom_max_order = CONFIG_PTDOM_MAX_ORDER;
#endif

static int __init cf_check parse_max_order(const char *s)
{
    if ( *s != ',' )
        domu_max_order = simple_strtoul(s, &s, 0);
    if ( *s == ',' && *++s != ',' )
        ctldom_max_order = simple_strtoul(s, &s, 0);
    if ( *s == ',' && *++s != ',' )
        hwdom_max_order = simple_strtoul(s, &s, 0);
#ifdef CONFIG_HAS_PASSTHROUGH
    if ( *s == ',' && *++s != ',' )
        ptdom_max_order = simple_strtoul(s, &s, 0);
#endif

    return *s ? -EINVAL : 0;
}
custom_param("memop-max-order", parse_max_order);

static unsigned int max_order(const struct domain *d)
{
    unsigned int order = domu_max_order;

#ifdef CONFIG_HAS_PASSTHROUGH
    if ( cache_flush_permitted(d) && order < ptdom_max_order )
        order = ptdom_max_order;
#endif

    if ( is_control_domain(d) && order < ctldom_max_order )
        order = ctldom_max_order;

    if ( is_hardware_domain(d) && order < hwdom_max_order )
        order = hwdom_max_order;

    return min(order, MAX_ORDER + 0U);
}

/* Helper to copy a typesafe MFN to guest */
static inline
unsigned long __copy_mfn_to_guest_offset(PRTOS_GUEST_HANDLE(prtos_pfn_t) hnd,
                                         size_t off, mfn_t mfn)
 {
    prtos_pfn_t mfn_ = mfn_x(mfn);

    return __copy_to_guest_offset(hnd, off, &mfn_, 1);
}

static void increase_reservation(struct memop_args *a)
{
    struct page_info *page;
    unsigned long i;
    struct domain *d = a->domain;

    if ( !guest_handle_is_null(a->extent_list) &&
         !guest_handle_subrange_okay(a->extent_list, a->nr_done,
                                     a->nr_extents-1) )
        return;

    if ( a->extent_order > max_order(current->domain) )
        return;

    for ( i = a->nr_done; i < a->nr_extents; i++ )
    {
        if ( i != a->nr_done && hypercall_preempt_check() )
        {
            a->preempted = 1;
            goto out;
        }

        page = alloc_domheap_pages(d, a->extent_order, a->memflags);
        if ( unlikely(page == NULL) ) 
        {
            gdprintk(PRTOSLOG_INFO, "Could not allocate order=%d extent: "
                    "id=%d memflags=%x (%ld of %d)\n",
                     a->extent_order, d->domain_id, a->memflags,
                     i, a->nr_extents);
            goto out;
        }

        /* Inform the domain of the new page's machine address. */ 
        if ( !paging_mode_translate(d) &&
             !guest_handle_is_null(a->extent_list) )
        {
            mfn_t mfn = page_to_mfn(page);

            if ( unlikely(__copy_mfn_to_guest_offset(a->extent_list, i, mfn)) )
                goto out;
        }
    }

 out:
    a->nr_done = i;
}

static void populate_physmap(struct memop_args *a)
{
    struct page_info *page;
    unsigned int i, j;
    prtos_pfn_t gpfn;
    struct domain *d = a->domain, *curr_d = current->domain;
    bool need_tlbflush = false;
    uint32_t tlbflush_timestamp = 0;

    if ( !guest_handle_subrange_okay(a->extent_list, a->nr_done,
                                     a->nr_extents-1) )
        return;

    if ( a->extent_order > (a->memflags & MEMF_populate_on_demand ? MAX_ORDER :
                            max_order(curr_d)) )
        return;

    if ( unlikely(!d->creation_finished) )
    {
        /*
         * With MEMF_no_tlbflush set, alloc_heap_pages() will ignore
         * TLB-flushes. After VM creation, this is a security issue (it can
         * make pages accessible to guest B, when guest A may still have a
         * cached mapping to them). So we do this only during domain creation,
         * when the domain itself has not yet been unpaused for the first
         * time.
         */
        a->memflags |= MEMF_no_tlbflush;
        /*
         * With MEMF_no_icache_flush, alloc_heap_pages() will skip
         * performing icache flushes. We do it only before domain
         * creation as once the domain is running there is a danger of
         * executing instructions from stale caches if icache flush is
         * delayed.
         */
        a->memflags |= MEMF_no_icache_flush;
    }

    for ( i = a->nr_done; i < a->nr_extents; i++ )
    {
        mfn_t mfn;

        if ( i != a->nr_done && hypercall_preempt_check() )
        {
            a->preempted = 1;
            goto out;
        }

        if ( unlikely(__copy_from_guest_offset(&gpfn, a->extent_list, i, 1)) )
            goto out;

        if ( a->memflags & MEMF_populate_on_demand )
        {
            /* Disallow populating PoD pages on oneself. */
            if ( d == curr_d )
                goto out;

            if ( is_hvm_domain(d) &&
                 guest_physmap_mark_populate_on_demand(d, gpfn,
                                                       a->extent_order) < 0 )
                goto out;
        }
        else
        {
            if ( is_domain_direct_mapped(d) )
            {
                mfn = _mfn(gpfn);

                for ( j = 0; j < (1U << a->extent_order); j++,
                      mfn = mfn_add(mfn, 1) )
                {
                    if ( !mfn_valid(mfn) )
                    {
                        gdprintk(PRTOSLOG_INFO, "Invalid mfn %#"PRI_mfn"\n",
                                 mfn_x(mfn));
                        goto out;
                    }

                    page = mfn_to_page(mfn);
                    if ( !get_page(page, d) )
                    {
                        gdprintk(PRTOSLOG_INFO,
                                 "mfn %#"PRI_mfn" doesn't belong to d%d\n",
                                  mfn_x(mfn), d->domain_id);
                        goto out;
                    }
                    put_page(page);
                }

                mfn = _mfn(gpfn);
            }
            else if ( is_domain_using_staticmem(d) )
            {
                /*
                 * No easy way to guarantee the retrieved pages are contiguous,
                 * so forbid non-zero-order requests here.
                 */
                if ( a->extent_order != 0 )
                {
                    gdprintk(PRTOSLOG_WARNING,
                             "Cannot allocate static order-%u pages for %pd\n",
                             a->extent_order, d);
                    goto out;
                }

                mfn = acquire_reserved_page(d, a->memflags);
                if ( mfn_eq(mfn, INVALID_MFN) )
                {
                    gdprintk(PRTOSLOG_WARNING,
                             "%pd: failed to retrieve a reserved page\n",
                             d);
                    goto out;
                }
            }
            else
            {
                page = alloc_domheap_pages(d, a->extent_order, a->memflags);

                if ( unlikely(!page) )
                {
                    gdprintk(PRTOSLOG_INFO,
                             "Could not allocate order=%u extent: id=%d memflags=%#x (%u of %u)\n",
                             a->extent_order, d->domain_id, a->memflags,
                             i, a->nr_extents);
                    goto out;
                }

                if ( unlikely(a->memflags & MEMF_no_tlbflush) )
                {
                    for ( j = 0; j < (1U << a->extent_order); j++ )
                        accumulate_tlbflush(&need_tlbflush, &page[j],
                                            &tlbflush_timestamp);
                }

                mfn = page_to_mfn(page);
            }

            if ( guest_physmap_add_page(d, _gfn(gpfn), mfn, a->extent_order) )
                goto out;

            if ( !paging_mode_translate(d) &&
                 /* Inform the domain of the new page's machine address. */
                 unlikely(__copy_mfn_to_guest_offset(a->extent_list, i, mfn)) )
                goto out;
        }
    }

out:
    if ( need_tlbflush )
        filtered_flush_tlb_mask(tlbflush_timestamp);

    if ( a->memflags & MEMF_no_icache_flush )
        invalidate_icache();

    a->nr_done = i;
}

int guest_remove_page(struct domain *d, unsigned long gmfn)
{
    struct page_info *page;
#ifdef CONFIG_X86
    p2m_type_t p2mt;
#endif
    mfn_t mfn;
#ifdef CONFIG_HAS_PASSTHROUGH
    bool *dont_flush_p, dont_flush;
#endif
    int rc;

#ifdef CONFIG_X86
    mfn = get_gfn_query(d, gmfn, &p2mt);
    if ( unlikely(p2mt == p2m_invalid) || unlikely(p2mt == p2m_mmio_dm) )
    {
        put_gfn(d, gmfn);

        return -ENOENT;
    }

    if ( unlikely(p2m_is_paging(p2mt)) )
    {
        /*
         * If the page hasn't yet been paged out, there is an
         * actual page that needs to be released.
         */
        if ( p2mt == p2m_ram_paging_out )
        {
            ASSERT(mfn_valid(mfn));
            goto obtain_page;
        }

        rc = guest_physmap_remove_page(d, _gfn(gmfn), mfn, 0);
        if ( rc )
            goto out_put_gfn;

        put_gfn(d, gmfn);

        p2m_mem_paging_drop_page(d, _gfn(gmfn), p2mt);

        return 0;
    }
    if ( p2mt == p2m_mmio_direct )
    {
        rc = -EPERM;
        goto out_put_gfn;
    }
#else
    mfn = gfn_to_mfn(d, _gfn(gmfn));
#endif
    if ( unlikely(!mfn_valid(mfn)) )
    {
#ifdef CONFIG_X86
        put_gfn(d, gmfn);
#endif
        gdprintk(PRTOSLOG_INFO, "Domain %u page number %lx invalid\n",
                d->domain_id, gmfn);

        return -EINVAL;
    }
            
#ifdef CONFIG_X86
    if ( p2m_is_shared(p2mt) )
    {
        /*
         * Unshare the page, bail out on error. We unshare because we
         * might be the only one using this shared page, and we need to
         * trigger proper cleanup. Once done, this is like any other page.
         */
        rc = mem_sharing_unshare_page(d, gmfn);
        if ( rc )
        {
            mem_sharing_notify_enomem(d, gmfn, false);
            goto out_put_gfn;
        }
        /* Maybe the mfn changed */
        mfn = get_gfn_query_unlocked(d, gmfn, &p2mt);
        ASSERT(!p2m_is_shared(p2mt));
    }
#endif /* CONFIG_X86 */

 obtain_page: __maybe_unused;
    page = mfn_to_page(mfn);
    if ( unlikely(!get_page(page, d)) )
    {
#ifdef CONFIG_X86
        put_gfn(d, gmfn);
        if ( !p2m_is_paging(p2mt) )
#endif
            gdprintk(PRTOSLOG_INFO, "Bad page free for Dom%u GFN %lx\n",
                     d->domain_id, gmfn);

        return -ENXIO;
    }

    /*
     * Since we're likely to free the page below, we need to suspend
     * prtosmem_add_to_physmap()'s suppressing of IOMMU TLB flushes.
     */
#ifdef CONFIG_HAS_PASSTHROUGH
    dont_flush_p = &this_cpu(iommu_dont_flush_iotlb);
    dont_flush = *dont_flush_p;
    *dont_flush_p = false;
#endif

    rc = guest_physmap_remove_page(d, _gfn(gmfn), mfn, 0);

#ifdef CONFIG_HAS_PASSTHROUGH
    *dont_flush_p = dont_flush;
#endif

    /*
     * With the lack of an IOMMU on some platforms, domains with DMA-capable
     * device must retrieve the same pfn when the hypercall populate_physmap
     * is called.
     *
     * For this purpose (and to match populate_physmap() behavior), the page
     * is kept allocated.
     */
    if ( !rc && !is_domain_direct_mapped(d) )
        put_page_alloc_ref(page);

    put_page(page);

#ifdef CONFIG_X86
 out_put_gfn:
    put_gfn(d, gmfn);
#endif

    /*
     * Filter out -ENOENT return values that aren't a result of an empty p2m
     * entry.
     */
    return rc != -ENOENT ? rc : -EINVAL;
}

static void decrease_reservation(struct memop_args *a)
{
    unsigned long i, j;
    prtos_pfn_t gmfn;

    if ( !guest_handle_subrange_okay(a->extent_list, a->nr_done,
                                     a->nr_extents-1) ||
         a->extent_order > max_order(current->domain) )
        return;

    for ( i = a->nr_done; i < a->nr_extents; i++ )
    {
        unsigned long pod_done;

        if ( i != a->nr_done && hypercall_preempt_check() )
        {
            a->preempted = 1;
            goto out;
        }

        if ( unlikely(__copy_from_guest_offset(&gmfn, a->extent_list, i, 1)) )
            goto out;

        if ( tb_init_done )
        {
            struct {
                uint64_t gfn;
                uint32_t d, order;
            } t;

            t.gfn = gmfn;
            t.d = a->domain->domain_id;
            t.order = a->extent_order;
        
            trace(TRC_MEM_DECREASE_RESERVATION, sizeof(t), &t);
        }

        /* See if populate-on-demand wants to handle this */
        pod_done = is_hvm_domain(a->domain) ?
                   p2m_pod_decrease_reservation(a->domain, _gfn(gmfn),
                                                a->extent_order) : 0;

        /*
         * Look for pages not handled by p2m_pod_decrease_reservation().
         *
         * guest_remove_page() will return -ENOENT for pages which have already
         * been removed by p2m_pod_decrease_reservation(); so expect to see
         * exactly pod_done failures.  Any more means that there were invalid
         * entries before p2m_pod_decrease_reservation() was called.
         */
        for ( j = 0; j + pod_done < (1UL << a->extent_order); j++ )
        {
            switch ( guest_remove_page(a->domain, gmfn + j) )
            {
            case 0:
                break;
            case -ENOENT:
                if ( !pod_done )
                    goto out;
                --pod_done;
                break;
            default:
                goto out;
            }
        }
    }

 out:
    a->nr_done = i;
}

static bool propagate_node(unsigned int xmf, unsigned int *memflags)
{
    const struct domain *currd = current->domain;

    BUILD_BUG_ON(PRTOSMEMF_get_node(0) != NUMA_NO_NODE);
    BUILD_BUG_ON(MEMF_get_node(0) != NUMA_NO_NODE);

    if ( PRTOSMEMF_get_node(xmf) == NUMA_NO_NODE )
        return true;

    if ( is_hardware_domain(currd) || is_control_domain(currd) )
    {
        if ( PRTOSMEMF_get_node(xmf) >= MAX_NUMNODES )
            return false;

        *memflags |= MEMF_node(PRTOSMEMF_get_node(xmf));
        if ( xmf & PRTOSMEMF_exact_node_request )
            *memflags |= MEMF_exact_node;
    }
    else if ( xmf & PRTOSMEMF_exact_node_request )
        return false;

    return true;
}

static long memory_exchange(PRTOS_GUEST_HANDLE_PARAM(prtos_memory_exchange_t) arg)
{
    struct prtos_memory_exchange exch;
    PAGE_LIST_HEAD(in_chunk_list);
    PAGE_LIST_HEAD(out_chunk_list);
    unsigned long in_chunk_order, out_chunk_order;
    prtos_pfn_t     gpfn, gmfn;
    mfn_t         mfn;
    unsigned long i, j, k;
    unsigned int  memflags = 0;
    long          rc = 0;
    struct domain *d;
    struct page_info *page;

    if ( copy_from_guest(&exch, arg, 1) )
        return -EFAULT;

    if ( max(exch.in.extent_order, exch.out.extent_order) >
         max_order(current->domain) )
    {
        rc = -EPERM;
        goto fail_early;
    }

    /* Various sanity checks. */
    if ( (exch.nr_exchanged > exch.in.nr_extents) ||
         /* Input and output domain identifiers match? */
         (exch.in.domid != exch.out.domid) ||
         /* Sizes of input and output lists do not overflow a long? */
         ((~0UL >> exch.in.extent_order) < exch.in.nr_extents) ||
         ((~0UL >> exch.out.extent_order) < exch.out.nr_extents) ||
         /* Sizes of input and output lists match? */
         ((exch.in.nr_extents << exch.in.extent_order) !=
          (exch.out.nr_extents << exch.out.extent_order)) )
    {
        rc = -EINVAL;
        goto fail_early;
    }

    if ( exch.nr_exchanged == exch.in.nr_extents )
        return 0;

    if ( !guest_handle_subrange_okay(exch.in.extent_start, exch.nr_exchanged,
                                     exch.in.nr_extents - 1) )
    {
        rc = -EFAULT;
        goto fail_early;
    }

    if ( exch.in.extent_order <= exch.out.extent_order )
    {
        in_chunk_order  = exch.out.extent_order - exch.in.extent_order;
        out_chunk_order = 0;

        if ( !guest_handle_subrange_okay(exch.out.extent_start,
                                         exch.nr_exchanged >> in_chunk_order,
                                         exch.out.nr_extents - 1) )
        {
            rc = -EFAULT;
            goto fail_early;
        }
    }
    else
    {
        in_chunk_order  = 0;
        out_chunk_order = exch.in.extent_order - exch.out.extent_order;

        if ( !guest_handle_subrange_okay(exch.out.extent_start,
                                         exch.nr_exchanged << out_chunk_order,
                                         exch.out.nr_extents - 1) )
        {
            rc = -EFAULT;
            goto fail_early;
        }
    }

    if ( unlikely(!propagate_node(exch.out.mem_flags, &memflags)) )
    {
        rc = -EINVAL;
        goto fail_early;
    }

    d = rcu_lock_domain_by_any_id(exch.in.domid);
    if ( d == NULL )
    {
        rc = -ESRCH;
        goto fail_early;
    }

    rc = xsm_memory_exchange(XSM_TARGET, d);
    if ( rc )
    {
        rcu_unlock_domain(d);
        goto fail_early;
    }

    memflags |= MEMF_bits(domain_clamp_alloc_bitsize(
        d,
        PRTOSMEMF_get_address_bits(exch.out.mem_flags) ? :
        (BITS_PER_LONG+PAGE_SHIFT)));

    for ( i = (exch.nr_exchanged >> in_chunk_order);
          i < (exch.in.nr_extents >> in_chunk_order);
          i++ )
    {
        if ( i != (exch.nr_exchanged >> in_chunk_order) &&
             hypercall_preempt_check() )
        {
            exch.nr_exchanged = i << in_chunk_order;
            rcu_unlock_domain(d);
            if ( __copy_field_to_guest(arg, &exch, nr_exchanged) )
                return -EFAULT;
            return hypercall_create_continuation(
                __HYPERVISOR_memory_op, "lh", PRTOSMEM_exchange, arg);
        }

        /* Steal a chunk's worth of input pages from the domain. */
        for ( j = 0; j < (1UL << in_chunk_order); j++ )
        {
            if ( unlikely(__copy_from_guest_offset(
                &gmfn, exch.in.extent_start, (i<<in_chunk_order)+j, 1)) )
            {
                rc = -EFAULT;
                goto fail;
            }

            for ( k = 0; k < (1UL << exch.in.extent_order); k++ )
            {
#ifdef CONFIG_X86
                p2m_type_t p2mt;

                /* Shared pages cannot be exchanged */
                mfn = get_gfn_unshare(d, gmfn + k, &p2mt);
                if ( p2m_is_shared(p2mt) )
                {
                    put_gfn(d, gmfn + k);
                    rc = -ENOMEM;
                    goto fail; 
                }
#else /* !CONFIG_X86 */
                mfn = gfn_to_mfn(d, _gfn(gmfn + k));
#endif
                if ( unlikely(!mfn_valid(mfn)) )
                {
#ifdef CONFIG_X86
                    put_gfn(d, gmfn + k);
#endif
                    rc = -EINVAL;
                    goto fail;
                }

                page = mfn_to_page(mfn);

                rc = steal_page(d, page, MEMF_no_refcount);
                if ( unlikely(rc) )
                {
#ifdef CONFIG_X86
                    put_gfn(d, gmfn + k);
#endif
                    goto fail;
                }

                page_list_add(page, &in_chunk_list);
#ifdef CONFIG_X86
                put_gfn(d, gmfn + k);
#endif
            }
        }

        /* Allocate a chunk's worth of anonymous output pages. */
        for ( j = 0; j < (1UL << out_chunk_order); j++ )
        {
            page = alloc_domheap_pages(d, exch.out.extent_order,
                                       MEMF_no_owner | memflags);
            if ( unlikely(page == NULL) )
            {
                rc = -ENOMEM;
                goto fail;
            }

            page_list_add(page, &out_chunk_list);
        }

        /*
         * Success! Beyond this point we cannot fail for this chunk.
         */

        /*
         * These pages have already had owner and reference cleared.
         * Do the final two steps: Remove from the physmap, and free
         * them.
         */
        while ( (page = page_list_remove_head(&in_chunk_list)) )
        {
            gfn_t gfn;

            mfn = page_to_mfn(page);
            gfn = mfn_to_gfn(d, mfn);
            /* Pages were unshared above */
            BUG_ON(SHARED_M2P(gfn_x(gfn)));
            if ( guest_physmap_remove_page(d, gfn, mfn, 0) )
                domain_crash(d);
            free_domheap_page(page);
        }

        /* Assign each output page to the domain. */
        for ( j = 0; (page = page_list_remove_head(&out_chunk_list)); ++j )
        {
            if ( assign_page(page, exch.out.extent_order, d,
                             MEMF_no_refcount) )
            {
                unsigned long dec_count;
                bool drop_dom_ref;

                /*
                 * Pages in in_chunk_list is stolen without
                 * decreasing the tot_pages. If the domain is dying when
                 * assign pages, we need decrease the count. For those pages
                 * that has been assigned, it should be covered by
                 * domain_relinquish_resources().
                 */
                dec_count = (((1UL << exch.in.extent_order) *
                              (1UL << in_chunk_order)) -
                             (j * (1UL << exch.out.extent_order)));

                nrspin_lock(&d->page_alloc_lock);
                drop_dom_ref = (dec_count &&
                                !domain_adjust_tot_pages(d, -dec_count));
                nrspin_unlock(&d->page_alloc_lock);

                if ( drop_dom_ref )
                    put_domain(d);

                free_domheap_pages(page, exch.out.extent_order);
                goto dying;
            }

            if ( __copy_from_guest_offset(&gpfn, exch.out.extent_start,
                                          (i << out_chunk_order) + j, 1) )
            {
                rc = -EFAULT;
                continue;
            }

            mfn = page_to_mfn(page);
            rc = guest_physmap_add_page(d, _gfn(gpfn), mfn,
                                        exch.out.extent_order) ?: rc;

            if ( !paging_mode_translate(d) &&
                 __copy_mfn_to_guest_offset(exch.out.extent_start,
                                            (i << out_chunk_order) + j,
                                            mfn) )
                rc = -EFAULT;
        }
        BUG_ON( !(d->is_dying) && (j != (1UL << out_chunk_order)) );

        if ( rc )
            goto fail;
    }

    exch.nr_exchanged = exch.in.nr_extents;
    if ( __copy_field_to_guest(arg, &exch, nr_exchanged) )
        rc = -EFAULT;
    rcu_unlock_domain(d);
    return rc;

    /*
     * Failed a chunk! Free any partial chunk work. Tell caller how many
     * chunks succeeded.
     */
 fail:
    /*
     * Reassign any input pages we managed to steal.  NB that if the assign
     * fails again, we're on the hook for freeing the page, since we've already
     * cleared PGC_allocated.
     */
    while ( (page = page_list_remove_head(&in_chunk_list)) )
        if ( assign_pages(page, 1, d, MEMF_no_refcount) )
        {
            BUG_ON(!d->is_dying);
            free_domheap_page(page);
        }

 dying:
    rcu_unlock_domain(d);
    /* Free any output pages we managed to allocate. */
    while ( (page = page_list_remove_head(&out_chunk_list)) )
        free_domheap_pages(page, exch.out.extent_order);

    exch.nr_exchanged = i << in_chunk_order;

 fail_early:
    if ( __copy_field_to_guest(arg, &exch, nr_exchanged) )
        rc = -EFAULT;
    return rc;
}

int prtosmem_add_to_physmap(struct domain *d, struct prtos_add_to_physmap *xatp,
                          unsigned int start)
{
    unsigned int done = 0;
    long rc = 0;
    union add_to_physmap_extra extra = {};
    struct page_info *pages[16];

    if ( !paging_mode_translate(d) )
    {
        ASSERT_UNREACHABLE();
        return -EACCES;
    }

    if ( gfn_eq(_gfn(xatp->gpfn), INVALID_GFN) )
        return -EINVAL;

    if ( xatp->space == PRTOSMAPSPACE_gmfn_foreign )
        extra.foreign_domid = DOMID_INVALID;

    if ( xatp->space != PRTOSMAPSPACE_gmfn_range )
        return prtosmem_add_to_physmap_one(d, xatp->space, extra,
                                         xatp->idx, _gfn(xatp->gpfn));

    if ( xatp->size < start )
        return -EILSEQ;

    if ( xatp->gpfn + xatp->size < xatp->gpfn ||
         xatp->idx + xatp->size < xatp->idx )
    {
        /*
         * Make sure INVALID_GFN is the highest representable value, i.e.
         * guaranteeing that it won't fall in the middle of the
         * [xatp->gpfn, xatp->gpfn + xatp->size) range checked above.
         */
        BUILD_BUG_ON(INVALID_GFN_RAW + 1);
        return -EOVERFLOW;
    }

    xatp->idx += start;
    xatp->gpfn += start;
    xatp->size -= start;

#ifdef CONFIG_HAS_PASSTHROUGH
    if ( is_iommu_enabled(d) )
    {
       this_cpu(iommu_dont_flush_iotlb) = 1;
       extra.ppage = &pages[0];
    }
#endif

    while ( xatp->size > done )
    {
        rc = prtosmem_add_to_physmap_one(d, PRTOSMAPSPACE_gmfn, extra,
                                       xatp->idx, _gfn(xatp->gpfn));
        if ( rc < 0 )
            break;

        xatp->idx++;
        xatp->gpfn++;

        if ( extra.ppage )
            ++extra.ppage;

        /* Check for continuation if it's not the last iteration. */
        if ( xatp->size > ++done &&
             ((done >= ARRAY_SIZE(pages) && extra.ppage) ||
              hypercall_preempt_check()) )
        {
            rc = start + done;
            break;
        }
    }

#ifdef CONFIG_HAS_PASSTHROUGH
    if ( is_iommu_enabled(d) )
    {
        int ret;
        unsigned int i;

        this_cpu(iommu_dont_flush_iotlb) = 0;

        ret = iommu_iotlb_flush(d, _dfn(xatp->idx - done), done,
                                IOMMU_FLUSHF_modified);
        if ( unlikely(ret) && rc >= 0 )
            rc = ret;

        /*
         * Now that the IOMMU TLB flush was done for the original GFN, drop
         * the page references. The 2nd flush below is fine to make later, as
         * whoever removes the page again from its new GFN will have to do
         * another flush anyway.
         */
        for ( i = 0; i < done; ++i )
            put_page(pages[i]);

        ret = iommu_iotlb_flush(d, _dfn(xatp->gpfn - done), done,
                                IOMMU_FLUSHF_added | IOMMU_FLUSHF_modified);
        if ( unlikely(ret) && rc >= 0 )
            rc = ret;
    }
#endif

    return rc;
}

static int prtosmem_add_to_physmap_batch(struct domain *d,
                                       struct prtos_add_to_physmap_batch *xatpb,
                                       unsigned int extent)
{
    union add_to_physmap_extra extra = {};

    /*
     * In some configurations, (!HVM, COVERAGE), the prtosmem_add_to_physmap_one()
     * call doesn't succumb to dead-code-elimination. Duplicate the short-circut
     * from xatp_permission_check() to try and help the compiler out.
     */
    if ( !paging_mode_translate(d) )
    {
        ASSERT_UNREACHABLE();
        return -EACCES;
    }

    if ( unlikely(xatpb->size < extent) )
        return -EILSEQ;

    if ( unlikely(xatpb->size == extent) )
        return extent ? -EILSEQ : 0;

    if ( !guest_handle_subrange_okay(xatpb->idxs, extent, xatpb->size - 1) ||
         !guest_handle_subrange_okay(xatpb->gpfns, extent, xatpb->size - 1) ||
         !guest_handle_subrange_okay(xatpb->errs, extent, xatpb->size - 1) )
        return -EFAULT;

    switch ( xatpb->space )
    {
    case PRTOSMAPSPACE_dev_mmio:
        /* res0 is reserved for future use. */
        if ( xatpb->u.res0 )
            return -EOPNOTSUPP;
        break;

    case PRTOSMAPSPACE_gmfn_foreign:
        extra.foreign_domid = xatpb->u.foreign_domid;
        break;
    }

    while ( xatpb->size > extent )
    {
        prtos_ulong_t idx;
        prtos_pfn_t gpfn;
        int rc;

        if ( unlikely(__copy_from_guest_offset(&idx, xatpb->idxs,
                                               extent, 1)) ||
             unlikely(__copy_from_guest_offset(&gpfn, xatpb->gpfns,
                                               extent, 1)) )
            return -EFAULT;

        if ( gfn_eq(_gfn(gpfn), INVALID_GFN) )
            return -EINVAL;

        rc = prtosmem_add_to_physmap_one(d, xatpb->space, extra,
                                       idx, _gfn(gpfn));

        if ( unlikely(__copy_to_guest_offset(xatpb->errs, extent, &rc, 1)) )
            return -EFAULT;

        /* Check for continuation if it's not the last iteration. */
        if ( xatpb->size > ++extent && hypercall_preempt_check() )
            return extent;
    }

    return 0;
}

static int construct_memop_from_reservation(
               const struct prtos_memory_reservation *r,
               struct memop_args *a)
{
    unsigned int address_bits;

    a->extent_list  = r->extent_start;
    a->nr_extents   = r->nr_extents;
    a->extent_order = r->extent_order;
    a->memflags     = 0;

    address_bits = PRTOSMEMF_get_address_bits(r->mem_flags);
    if ( (address_bits != 0) &&
         (address_bits < (get_order_from_pages(max_page) + PAGE_SHIFT)) )
    {
        if ( address_bits <= PAGE_SHIFT )
            return -EINVAL;
        a->memflags = MEMF_bits(address_bits);
    }

    if ( r->mem_flags & PRTOSMEMF_vnode )
    {
        nodeid_t vnode, pnode;
        struct domain *d = a->domain;

        read_lock(&d->vnuma_rwlock);
        if ( d->vnuma )
        {
            vnode = PRTOSMEMF_get_node(r->mem_flags);
            if ( vnode >= d->vnuma->nr_vnodes )
            {
                read_unlock(&d->vnuma_rwlock);
                return -EINVAL;
            }

            pnode = d->vnuma->vnode_to_pnode[vnode];
            if ( pnode != NUMA_NO_NODE )
            {
                a->memflags |= MEMF_node(pnode);
                if ( r->mem_flags & PRTOSMEMF_exact_node_request )
                    a->memflags |= MEMF_exact_node;
            }
        }
        read_unlock(&d->vnuma_rwlock);
    }
    else if ( unlikely(!propagate_node(r->mem_flags, &a->memflags)) )
        return -EINVAL;

    return 0;
}

#ifdef CONFIG_HAS_PASSTHROUGH
struct get_reserved_device_memory {
    struct prtos_reserved_device_memory_map map;
    unsigned int used_entries;
};

static int cf_check get_reserved_device_memory(
    prtos_pfn_t start, prtos_ulong_t nr, u32 id, void *ctxt)
{
    struct get_reserved_device_memory *grdm = ctxt;
    uint32_t sbdf = PCI_SBDF(grdm->map.dev.pci.seg, grdm->map.dev.pci.bus,
                             grdm->map.dev.pci.devfn).sbdf;

    if ( !(grdm->map.flags & PRTOSMEM_RDM_ALL) && (sbdf != id) )
        return 0;

    if ( !nr )
        return 1;

    if ( grdm->used_entries < grdm->map.nr_entries )
    {
        struct prtos_reserved_device_memory rdm = {
            .start_pfn = start, .nr_pages = nr
        };

        if ( __copy_to_guest_offset(grdm->map.buffer, grdm->used_entries,
                                    &rdm, 1) )
            return -EFAULT;
    }

    ++grdm->used_entries;

    return 1;
}
#endif

static long xatp_permission_check(struct domain *d, unsigned int space)
{
    if ( !paging_mode_translate(d) )
        return -EACCES;

    /*
     * PRTOSMAPSPACE_dev_mmio mapping is only supported for hardware Domain
     * to map this kind of space to itself.
     */
    if ( (space == PRTOSMAPSPACE_dev_mmio) &&
         (!is_hardware_domain(d) || (d != current->domain)) )
        return -EACCES;

    return xsm_add_to_physmap(XSM_TARGET, current->domain, d);
}

static unsigned int ioreq_server_max_frames(const struct domain *d)
{
    unsigned int nr = 0;

#ifdef CONFIG_IOREQ_SERVER
    if ( is_hvm_domain(d) )
        /* One frame for the buf-ioreq ring, and one frame per 128 vcpus. */
        nr = 1 + DIV_ROUND_UP(d->max_vcpus * sizeof(struct ioreq), PAGE_SIZE);
#endif

    return nr;
}

/*
 * Return 0 on any kind of error.  Caller converts to -EINVAL.
 *
 * All nonzero values should be repeatable (i.e. derived from some fixed
 * property of the domain), and describe the full resource (i.e. mapping the
 * result of this call will be the entire resource).
 */
static unsigned int resource_max_frames(const struct domain *d,
                                        unsigned int type, unsigned int id)
{
    switch ( type )
    {
    case PRTOSMEM_resource_grant_table:
        return gnttab_resource_max_frames(d, id);

    case PRTOSMEM_resource_ioreq_server:
        return ioreq_server_max_frames(d);

    case PRTOSMEM_resource_vmtrace_buf:
        return d->vmtrace_size >> PAGE_SHIFT;

    default:
        return -EOPNOTSUPP;
    }
}

static int acquire_ioreq_server(struct domain *d,
                                unsigned int id,
                                unsigned int frame,
                                unsigned int nr_frames,
                                prtos_pfn_t mfn_list[])
{
#ifdef CONFIG_IOREQ_SERVER
    ioservid_t ioservid = id;
    unsigned int i;
    int rc;

    if ( !is_hvm_domain(d) )
        return -EINVAL;

    if ( id != (unsigned int)ioservid )
        return -EINVAL;

    for ( i = 0; i < nr_frames; i++ )
    {
        mfn_t mfn;

        rc = ioreq_server_get_frame(d, id, frame + i, &mfn);
        if ( rc )
            return rc;

        mfn_list[i] = mfn_x(mfn);
    }

    /* Success.  Passed nr_frames back to the caller. */
    return nr_frames;
#else
    return -EOPNOTSUPP;
#endif
}

static int acquire_vmtrace_buf(
    struct domain *d, unsigned int id, unsigned int frame,
    unsigned int nr_frames, prtos_pfn_t mfn_list[])
{
    const struct vcpu *v = domain_vcpu(d, id);
    unsigned int i;
    mfn_t mfn;

    if ( !v )
        return -ENOENT;

    if ( !v->vmtrace.pg ||
         (frame + nr_frames) > (d->vmtrace_size >> PAGE_SHIFT) )
        return -EINVAL;

    mfn = page_to_mfn(v->vmtrace.pg);

    for ( i = 0; i < nr_frames; i++ )
        mfn_list[i] = mfn_x(mfn) + frame + i;

    return nr_frames;
}

/*
 * Returns -errno on error, or positive in the range [1, nr_frames] on
 * success.  Returning less than nr_frames contitutes a request for a
 * continuation.  Callers can depend on frame + nr_frames not overflowing.
 */
static int _acquire_resource(
    struct domain *d, unsigned int type, unsigned int id, unsigned int frame,
    unsigned int nr_frames, prtos_pfn_t mfn_list[])
{
    switch ( type )
    {
    case PRTOSMEM_resource_grant_table:
        return gnttab_acquire_resource(d, id, frame, nr_frames, mfn_list);

    case PRTOSMEM_resource_ioreq_server:
        return acquire_ioreq_server(d, id, frame, nr_frames, mfn_list);

    case PRTOSMEM_resource_vmtrace_buf:
        return acquire_vmtrace_buf(d, id, frame, nr_frames, mfn_list);

    default:
        return -EOPNOTSUPP;
    }
}

static int acquire_resource(
    PRTOS_GUEST_HANDLE_PARAM(prtos_mem_acquire_resource_t) arg,
    unsigned long start_extent)
{
    struct domain *d, *currd = current->domain;
    prtos_mem_acquire_resource_t xmar;
    unsigned int max_frames;
    int rc;

    if ( !arch_acquire_resource_check(currd) )
        return -EACCES;

    if ( copy_from_guest(&xmar, arg, 1) )
        return -EFAULT;

    if ( xmar.pad != 0 )
        return -EINVAL;

    /*
     * The ABI is rather unfortunate.  nr_frames (and therefore the total size
     * of the resource) is 32bit, while frame (the offset within the resource
     * we'd like to start at) is 64bit.
     *
     * Reject values oustide the of the range of nr_frames, as well as
     * combinations of frame and nr_frame which overflow, to simplify the rest
     * of the logic.
     */
    if ( (xmar.frame >> 32) ||
         ((xmar.frame + xmar.nr_frames) >> 32) )
        return -EINVAL;

    rc = rcu_lock_remote_domain_by_id(xmar.domid, &d);
    if ( rc )
        return rc;

    rc = xsm_domain_resource_map(XSM_DM_PRIV, d);
    if ( rc )
        goto out;

    max_frames = resource_max_frames(d, xmar.type, xmar.id);

    rc = -EINVAL;
    if ( !max_frames )
        goto out;

    if ( guest_handle_is_null(xmar.frame_list) )
    {
        if ( xmar.nr_frames || start_extent )
            goto out;

        xmar.nr_frames = max_frames;
        rc = __copy_field_to_guest(arg, &xmar, nr_frames) ? -EFAULT : 0;
        goto out;
    }

    /*
     * Limiting nr_frames at (UINT_MAX >> MEMOP_EXTENT_SHIFT) isn't ideal.  If
     * it ever becomes a practical problem, we can switch to mutating
     * xmar.{frame,nr_frames,frame_list} in guest memory.
     */
    rc = -EINVAL;
    if ( start_extent >= xmar.nr_frames ||
         xmar.nr_frames > (UINT_MAX >> MEMOP_EXTENT_SHIFT) )
        goto out;

    /* Adjust for work done on previous continuations. */
    xmar.nr_frames -= start_extent;
    xmar.frame += start_extent;
    guest_handle_add_offset(xmar.frame_list, start_extent);

    do {
        /*
         * Arbitrary size.  Not too much stack space, and a reasonable stride
         * for continuation checks.
         */
        prtos_pfn_t mfn_list[32];
        unsigned int todo = MIN(ARRAY_SIZE(mfn_list), xmar.nr_frames), done;

        rc = _acquire_resource(d, xmar.type, xmar.id, xmar.frame,
                               todo, mfn_list);
        if ( rc < 0 )
            goto out;

        done = rc;
        rc = 0;
        if ( done == 0 || done > todo )
        {
            ASSERT_UNREACHABLE();
            rc = -EINVAL;
            goto out;
        }

        /* Adjust guest frame_list appropriately. */
        if ( !paging_mode_translate(currd) )
        {
            if ( copy_to_guest(xmar.frame_list, mfn_list, done) )
                rc = -EFAULT;
        }
        else
        {
            prtos_pfn_t gfn_list[ARRAY_SIZE(mfn_list)];
            unsigned int i;

            if ( copy_from_guest(gfn_list, xmar.frame_list, done) )
                rc = -EFAULT;

            for ( i = 0; !rc && i < done; i++ )
            {
                rc = set_foreign_p2m_entry(currd, d, gfn_list[i],
                                           _mfn(mfn_list[i]));
                /* rc should be -EIO for any iteration other than the first */
                if ( rc && i )
                    rc = -EIO;
            }
        }

        if ( rc )
            goto out;

        xmar.nr_frames -= done;
        xmar.frame += done;
        guest_handle_add_offset(xmar.frame_list, done);
        start_extent += done;

        /*
         * Explicit continuation request from _acquire_resource(), or we've
         * still got more work to do.
         */
        if ( done < todo ||
             (xmar.nr_frames && hypercall_preempt_check()) )
        {
            rc = hypercall_create_continuation(
                __HYPERVISOR_memory_op, "lh",
                PRTOSMEM_acquire_resource | (start_extent << MEMOP_EXTENT_SHIFT),
                arg);
            goto out;
        }

    } while ( xmar.nr_frames );

    rc = 0;

 out:
    rcu_unlock_domain(d);

    return rc;
}

long do_memory_op(unsigned long cmd, PRTOS_GUEST_HANDLE_PARAM(void) arg)
{
    struct domain *d, *curr_d = current->domain;
    long rc;
    struct prtos_memory_reservation reservation;
    struct memop_args args;
    unsigned long start_extent = cmd >> MEMOP_EXTENT_SHIFT;
    int op = cmd & MEMOP_CMD_MASK;

    switch ( op )
    {
    case PRTOSMEM_increase_reservation:
    case PRTOSMEM_decrease_reservation:
    case PRTOSMEM_populate_physmap:
        if ( copy_from_guest(&reservation, arg, 1) )
            return start_extent;

        /* Is size too large for us to encode a continuation? */
        if ( reservation.nr_extents > (UINT_MAX >> MEMOP_EXTENT_SHIFT) )
            return start_extent;

        if ( unlikely(start_extent >= reservation.nr_extents) )
            return start_extent;

        d = rcu_lock_domain_by_any_id(reservation.domid);
        if ( d == NULL )
            return start_extent;
        args.domain = d;

        if ( construct_memop_from_reservation(&reservation, &args) )
        {
            rcu_unlock_domain(d);
            return start_extent;
        }

        args.nr_done   = start_extent;
        args.preempted = 0;

        if ( op == PRTOSMEM_populate_physmap
             && (reservation.mem_flags & PRTOSMEMF_populate_on_demand) )
            args.memflags |= MEMF_populate_on_demand;

        if ( xsm_memory_adjust_reservation(XSM_TARGET, curr_d, d) )
        {
            rcu_unlock_domain(d);
            return start_extent;
        }

#ifdef CONFIG_X86
        if ( pv_shim && op != PRTOSMEM_decrease_reservation && !start_extent )
            /* Avoid calling pv_shim_online_memory when in a continuation. */
            pv_shim_online_memory(args.nr_extents, args.extent_order);
#endif

        switch ( op )
        {
        case PRTOSMEM_increase_reservation:
            increase_reservation(&args);
            break;
        case PRTOSMEM_decrease_reservation:
            decrease_reservation(&args);
            break;
        default: /* PRTOSMEM_populate_physmap */
            populate_physmap(&args);
            break;
        }

        rcu_unlock_domain(d);

        rc = args.nr_done;

#ifdef CONFIG_X86
        if ( pv_shim && op == PRTOSMEM_decrease_reservation )
            pv_shim_offline_memory(args.nr_done - start_extent,
                                   args.extent_order);
#endif

        if ( args.preempted )
           return hypercall_create_continuation(
                __HYPERVISOR_memory_op, "lh",
                op | (rc << MEMOP_EXTENT_SHIFT), arg);

        break;

    case PRTOSMEM_exchange:
        if ( unlikely(start_extent) )
            return -EINVAL;

        rc = memory_exchange(guest_handle_cast(arg, prtos_memory_exchange_t));
        break;

    case PRTOSMEM_maximum_ram_page:
        if ( unlikely(start_extent) )
            return -EINVAL;

        rc = max_page;
        break;

    case PRTOSMEM_current_reservation:
    case PRTOSMEM_maximum_reservation:
    case PRTOSMEM_maximum_gpfn:
    {
        struct prtos_memory_domain domain;

        if ( unlikely(start_extent) )
            return -EINVAL;

        if ( copy_from_guest(&domain, arg, 1) )
            return -EFAULT;

        d = rcu_lock_domain_by_any_id(domain.domid);
        if ( d == NULL )
            return -ESRCH;

        rc = xsm_memory_stat_reservation(XSM_TARGET, curr_d, d);
        if ( rc )
        {
            rcu_unlock_domain(d);
            return rc;
        }

        switch ( op )
        {
        case PRTOSMEM_current_reservation:
            rc = domain_tot_pages(d);
            break;
        case PRTOSMEM_maximum_reservation:
            rc = d->max_pages;
            break;
        default:
            ASSERT(op == PRTOSMEM_maximum_gpfn);
            rc = domain_get_maximum_gpfn(d);
            break;
        }

        rcu_unlock_domain(d);

        break;
    }

    case PRTOSMEM_add_to_physmap:
    {
        struct prtos_add_to_physmap xatp;

        BUILD_BUG_ON((typeof(xatp.size))-1 > (UINT_MAX >> MEMOP_EXTENT_SHIFT));

        /* Check for malicious or buggy input. */
        if ( start_extent != (typeof(xatp.size))start_extent )
            return -EDOM;

        if ( copy_from_guest(&xatp, arg, 1) )
            return -EFAULT;

        /* Foreign mapping is only possible via add_to_physmap_batch. */
        if ( xatp.space == PRTOSMAPSPACE_gmfn_foreign )
            return -ENOSYS;

        d = rcu_lock_domain_by_any_id(xatp.domid);
        if ( d == NULL )
            return -ESRCH;

        rc = xatp_permission_check(d, xatp.space);
        if ( rc )
        {
            rcu_unlock_domain(d);
            return rc;
        }

        rc = prtosmem_add_to_physmap(d, &xatp, start_extent);

        rcu_unlock_domain(d);

        if ( xatp.space == PRTOSMAPSPACE_gmfn_range && rc > 0 )
            rc = hypercall_create_continuation(
                     __HYPERVISOR_memory_op, "lh",
                     op | (rc << MEMOP_EXTENT_SHIFT), arg);

        return rc;
    }

    case PRTOSMEM_add_to_physmap_batch:
    {
        struct prtos_add_to_physmap_batch xatpb;

        BUILD_BUG_ON((typeof(xatpb.size))-1 >
                     (UINT_MAX >> MEMOP_EXTENT_SHIFT));

        /* Check for malicious or buggy input. */
        if ( start_extent != (typeof(xatpb.size))start_extent )
            return -EDOM;

        if ( copy_from_guest(&xatpb, arg, 1) )
            return -EFAULT;

        /* This mapspace is unsupported for this hypercall. */
        if ( xatpb.space == PRTOSMAPSPACE_gmfn_range )
            return -EOPNOTSUPP;

        d = rcu_lock_domain_by_any_id(xatpb.domid);
        if ( d == NULL )
            return -ESRCH;

        rc = xatp_permission_check(d, xatpb.space);
        if ( rc )
        {
            rcu_unlock_domain(d);
            return rc;
        }

        rc = prtosmem_add_to_physmap_batch(d, &xatpb, start_extent);

        rcu_unlock_domain(d);

        if ( rc > 0 )
            rc = hypercall_create_continuation(
                    __HYPERVISOR_memory_op, "lh",
                    op | (rc << MEMOP_EXTENT_SHIFT), arg);

        return rc;
    }

    case PRTOSMEM_remove_from_physmap:
    {
        struct prtos_remove_from_physmap xrfp;
        struct page_info *page;

        if ( unlikely(start_extent) )
            return -EINVAL;

        if ( copy_from_guest(&xrfp, arg, 1) )
            return -EFAULT;

        d = rcu_lock_domain_by_any_id(xrfp.domid);
        if ( d == NULL )
            return -ESRCH;

        rc = paging_mode_translate(d)
             ? xsm_remove_from_physmap(XSM_TARGET, curr_d, d)
             : -EACCES;
        if ( rc )
        {
            rcu_unlock_domain(d);
            return rc;
        }

        page = get_page_from_gfn(d, xrfp.gpfn, NULL, P2M_ALLOC);
        if ( page )
        {
            rc = guest_physmap_remove_page(d, _gfn(xrfp.gpfn),
                                           page_to_mfn(page), 0);
            put_page(page);
        }
        else
            rc = -ENOENT;

        rcu_unlock_domain(d);

        break;
    }

    case PRTOSMEM_access_op:
        rc = mem_access_memop(cmd, guest_handle_cast(arg, prtos_mem_access_op_t));
        break;

    case PRTOSMEM_claim_pages:
        if ( unlikely(start_extent) )
            return -EINVAL;

        if ( copy_from_guest(&reservation, arg, 1) )
            return -EFAULT;

        if ( !guest_handle_is_null(reservation.extent_start) )
            return -EINVAL;

        if ( reservation.extent_order != 0 )
            return -EINVAL;

        if ( reservation.mem_flags != 0 )
            return -EINVAL;

        d = rcu_lock_domain_by_id(reservation.domid);
        if ( d == NULL )
            return -EINVAL;

        rc = xsm_claim_pages(XSM_PRIV, d);

        if ( !rc )
            rc = domain_set_outstanding_pages(d, reservation.nr_extents);

        rcu_unlock_domain(d);

        break;

    case PRTOSMEM_get_vnumainfo:
    {
        struct prtos_vnuma_topology_info topology;
        unsigned int dom_vnodes, dom_vranges, dom_vcpus;
        struct vnuma_info tmp;

        if ( unlikely(start_extent) )
            return -EINVAL;

        /*
         * Guest passes nr_vnodes, number of regions and nr_vcpus thus
         * we know how much memory guest has allocated.
         */
        if ( copy_from_guest(&topology, arg, 1 ))
            return -EFAULT;

        if ( topology.pad != 0 )
            return -EINVAL;

        if ( (d = rcu_lock_domain_by_any_id(topology.domid)) == NULL )
            return -ESRCH;

        rc = xsm_get_vnumainfo(XSM_TARGET, d);
        if ( rc )
        {
            rcu_unlock_domain(d);
            return rc;
        }

        read_lock(&d->vnuma_rwlock);

        if ( d->vnuma == NULL )
        {
            read_unlock(&d->vnuma_rwlock);
            rcu_unlock_domain(d);
            return -EOPNOTSUPP;
        }

        dom_vnodes = d->vnuma->nr_vnodes;
        dom_vranges = d->vnuma->nr_vmemranges;
        dom_vcpus = d->max_vcpus;

        /*
         * Copied from guest values may differ from domain vnuma config.
         * Check here guest parameters make sure we dont overflow.
         * Additionaly check padding.
         */
        if ( topology.nr_vnodes < dom_vnodes      ||
             topology.nr_vcpus < dom_vcpus        ||
             topology.nr_vmemranges < dom_vranges )
        {
            read_unlock(&d->vnuma_rwlock);
            rcu_unlock_domain(d);

            topology.nr_vnodes = dom_vnodes;
            topology.nr_vcpus = dom_vcpus;
            topology.nr_vmemranges = dom_vranges;

            /* Copy back needed values. */
            return __copy_to_guest(arg, &topology, 1) ? -EFAULT : -ENOBUFS;
        }

        read_unlock(&d->vnuma_rwlock);

        tmp.vdistance = xmalloc_array(unsigned int, dom_vnodes * dom_vnodes);
        tmp.vmemrange = xmalloc_array(prtos_vmemrange_t, dom_vranges);
        tmp.vcpu_to_vnode = xmalloc_array(unsigned int, dom_vcpus);

        if ( tmp.vdistance == NULL ||
             tmp.vmemrange == NULL ||
             tmp.vcpu_to_vnode == NULL )
        {
            rc = -ENOMEM;
            goto vnumainfo_out;
        }

        /*
         * Check if vnuma info has changed and if the allocated arrays
         * are not big enough.
         */
        read_lock(&d->vnuma_rwlock);

        if ( dom_vnodes < d->vnuma->nr_vnodes ||
             dom_vranges < d->vnuma->nr_vmemranges ||
             dom_vcpus < d->max_vcpus )
        {
            read_unlock(&d->vnuma_rwlock);
            rc = -EAGAIN;
            goto vnumainfo_out;
        }

        dom_vnodes = d->vnuma->nr_vnodes;
        dom_vranges = d->vnuma->nr_vmemranges;
        dom_vcpus = d->max_vcpus;

        memcpy(tmp.vmemrange, d->vnuma->vmemrange,
               sizeof(*d->vnuma->vmemrange) * dom_vranges);
        memcpy(tmp.vdistance, d->vnuma->vdistance,
               sizeof(*d->vnuma->vdistance) * dom_vnodes * dom_vnodes);
        memcpy(tmp.vcpu_to_vnode, d->vnuma->vcpu_to_vnode,
               sizeof(*d->vnuma->vcpu_to_vnode) * dom_vcpus);

        read_unlock(&d->vnuma_rwlock);

        rc = -EFAULT;

        if ( copy_to_guest(topology.vmemrange.h, tmp.vmemrange,
                           dom_vranges) != 0 )
            goto vnumainfo_out;

        if ( copy_to_guest(topology.vdistance.h, tmp.vdistance,
                           dom_vnodes * dom_vnodes) != 0 )
            goto vnumainfo_out;

        if ( copy_to_guest(topology.vcpu_to_vnode.h, tmp.vcpu_to_vnode,
                           dom_vcpus) != 0 )
            goto vnumainfo_out;

        topology.nr_vnodes = dom_vnodes;
        topology.nr_vcpus = dom_vcpus;
        topology.nr_vmemranges = dom_vranges;

        rc = __copy_to_guest(arg, &topology, 1) ? -EFAULT : 0;

 vnumainfo_out:
        rcu_unlock_domain(d);

        xfree(tmp.vdistance);
        xfree(tmp.vmemrange);
        xfree(tmp.vcpu_to_vnode);
        break;
    }

#ifdef CONFIG_HAS_PASSTHROUGH
    case PRTOSMEM_reserved_device_memory_map:
    {
        struct get_reserved_device_memory grdm;

        if ( unlikely(start_extent) )
            return -EINVAL;

        if ( copy_from_guest(&grdm.map, arg, 1) ||
             !guest_handle_okay(grdm.map.buffer, grdm.map.nr_entries) )
            return -EFAULT;

        if ( grdm.map.flags & ~PRTOSMEM_RDM_ALL )
            return -EINVAL;

        grdm.used_entries = 0;
        rc = iommu_get_reserved_device_memory(get_reserved_device_memory,
                                              &grdm);

        if ( !rc && grdm.map.nr_entries < grdm.used_entries )
            rc = -ENOBUFS;
        grdm.map.nr_entries = grdm.used_entries;
        if ( __copy_to_guest(arg, &grdm.map, 1) )
            rc = -EFAULT;

        break;
    }
#endif

    case PRTOSMEM_acquire_resource:
        rc = acquire_resource(
            guest_handle_cast(arg, prtos_mem_acquire_resource_t),
            start_extent);
        break;

    default:
        rc = arch_memory_op(cmd, arg);
        break;
    }

    return rc;
}



void destroy_ring_for_helper(
    void **_va, struct page_info *page)
{
    void *va = *_va;

    if ( va != NULL )
    {
        unmap_domain_page_global(va);
        put_page_and_type(page);
        *_va = NULL;
    }
}

/*
 * Acquire a pointer to struct page_info for a specified domain and GFN,
 * checking whether the page has been paged out, or needs unsharing.
 * If the function succeeds then zero is returned, page_p is written
 * with a pointer to the struct page_info with a reference taken, and
 * p2mt_p it is written with the P2M type of the page. The caller is
 * responsible for dropping the reference.
 * If the function fails then an appropriate errno is returned and the
 * values referenced by page_p and p2mt_p are undefined.
 */
int check_get_page_from_gfn(struct domain *d, gfn_t gfn, bool readonly,
                            p2m_type_t *p2mt_p, struct page_info **page_p)
{
    p2m_query_t q = readonly ? P2M_ALLOC : P2M_UNSHARE;
    p2m_type_t p2mt;
    struct page_info *page;

    page = get_page_from_gfn(d, gfn_x(gfn), &p2mt, q);

#ifdef CONFIG_MEM_PAGING
    if ( p2m_is_paging(p2mt) )
    {
        if ( page )
            put_page(page);

        p2m_mem_paging_populate(d, gfn);
        return -EAGAIN;
    }
#endif
#ifdef CONFIG_MEM_SHARING
    if ( (q & P2M_UNSHARE) && p2m_is_shared(p2mt) )
    {
        if ( page )
            put_page(page);

        return -EAGAIN;
    }
#endif
#ifdef CONFIG_X86
    if ( p2mt == p2m_mmio_direct )
    {
        if ( page )
            put_page(page);

        return -EPERM;
    }
#endif

    if ( !page )
        return -EINVAL;

    *p2mt_p = p2mt;
    *page_p = page;
    return 0;
}

int prepare_ring_for_helper(
    struct domain *d, unsigned long gmfn, struct page_info **_page,
    void **_va)
{
    p2m_type_t p2mt;
    struct page_info *page;
    void *va;
    int rc;

    rc = check_get_page_from_gfn(d, _gfn(gmfn), false, &p2mt, &page);
    if ( rc )
        return (rc == -EAGAIN) ? -ENOENT : rc;

    if ( !get_page_type(page, PGT_writable_page) )
    {
        put_page(page);
        return -EINVAL;
    }

    va = __map_domain_page_global(page);
    if ( va == NULL )
    {
        put_page_and_type(page);
        return -ENOMEM;
    }

    *_va = va;
    *_page = page;

    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: memory.c === */
/* === BEGIN INLINED: pdx.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * Original code extracted from arch/x86/x86_64/mm.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <prtos_init.h>
#include <prtos_mm.h>
#include <prtos_bitops.h>
#include <prtos_nospec.h>


unsigned long __read_mostly pdx_group_valid[BITS_TO_LONGS(
    (FRAMETABLE_NR + PDX_GROUP_COUNT - 1) / PDX_GROUP_COUNT)] = { [0] = 1 };

bool __mfn_valid(unsigned long mfn)
{
    bool invalid = mfn >= max_page;

#ifdef CONFIG_PDX_COMPRESSION
    invalid |= mfn & pfn_hole_mask;
#endif

    if ( unlikely(evaluate_nospec(invalid)) )
        return false;

    return test_bit(pfn_to_pdx(mfn) / PDX_GROUP_COUNT, pdx_group_valid);
}

void set_pdx_range(unsigned long smfn, unsigned long emfn)
{
    unsigned long idx, eidx;

    idx = pfn_to_pdx(smfn) / PDX_GROUP_COUNT;
    eidx = (pfn_to_pdx(emfn - 1) + PDX_GROUP_COUNT) / PDX_GROUP_COUNT;

    for ( ; idx < eidx; ++idx )
        __set_bit(idx, pdx_group_valid);
}

#ifdef CONFIG_PDX_COMPRESSION

/*
 * Diagram to make sense of the following variables. The masks and shifts
 * are done on mfn values in order to convert to/from pdx:
 *
 *                      pfn_hole_mask
 *                      pfn_pdx_hole_shift (mask bitsize)
 *                      |
 *                 |---------|
 *                 |         |
 *                 V         V
 *         --------------------------
 *         |HHHHHHH|000000000|LLLLLL| <--- mfn
 *         --------------------------
 *         ^       ^         ^      ^
 *         |       |         |------|
 *         |       |             |
 *         |       |             pfn_pdx_bottom_mask
 *         |       |
 *         |-------|
 *             |
 *             pfn_top_mask
 *
 * ma_{top,va_bottom}_mask is simply a shifted pfn_{top,pdx_bottom}_mask,
 * where ma_top_mask has zeroes shifted in while ma_va_bottom_mask has
 * ones.
 */

/** Mask for the lower non-compressible bits of an mfn */
unsigned long __ro_after_init pfn_pdx_bottom_mask = ~0UL;

/** Mask for the lower non-compressible bits of an maddr or vaddr */
unsigned long __ro_after_init ma_va_bottom_mask = ~0UL;

/** Mask for the higher non-compressible bits of an mfn */
unsigned long __ro_after_init pfn_top_mask = 0;

/** Mask for the higher non-compressible bits of an maddr or vaddr */
unsigned long __ro_after_init ma_top_mask = 0;

/**
 * Mask for a pdx compression bit slice.
 *
 *  Invariant: valid(mfn) implies (mfn & pfn_hole_mask) == 0
 */
unsigned long __ro_after_init pfn_hole_mask = 0;

/** Number of bits of the "compressible" bit slice of an mfn */
unsigned int __ro_after_init pfn_pdx_hole_shift = 0;

/* Sets all bits from the most-significant 1-bit down to the LSB */
static uint64_t fill_mask(uint64_t mask)
{
    while (mask & (mask + 1))
        mask |= mask + 1;

    return mask;
}

bool pdx_is_region_compressible(paddr_t base, unsigned long npages)
{
    return !(paddr_to_pfn(base) & pfn_hole_mask) &&
           !(pdx_region_mask(base, npages * PAGE_SIZE) & ~ma_va_bottom_mask);
}

/* We don't want to compress the low MAX_ORDER bits of the addresses. */
uint64_t __init pdx_init_mask(uint64_t base_addr)
{
    return fill_mask(max(base_addr,
                         (uint64_t)1 << (MAX_ORDER + PAGE_SHIFT)) - 1);
}

uint64_t pdx_region_mask(uint64_t base, uint64_t len)
{
    /*
     * We say a bit "moves" in a range if there exist 2 addresses in that
     * range that have that bit both set and cleared respectively. We want
     * to create a mask of _all_ moving bits in this range. We do this by
     * comparing the first and last addresses in the range, discarding the
     * bits that remain the same (this is logically an XOR operation). The
     * MSB of the resulting expression is the most significant moving bit
     * in the range. Then it's a matter of setting every bit in lower
     * positions in order to get the mask of moving bits.
     */
    return fill_mask(base ^ (base + len - 1));
}

void __init pfn_pdx_hole_setup(unsigned long mask)
{
    unsigned int i, j, bottom_shift = 0, hole_shift = 0;

    /*
     * We skip the first MAX_ORDER bits, as we never want to compress them.
     * This guarantees that page-pointer arithmetic remains valid within
     * contiguous aligned ranges of 2^MAX_ORDER pages. Among others, our
     * buddy allocator relies on this assumption.
     *
     * If the logic changes here, we might have to update the ARM specific
     * init_pdx too.
     */
    for ( j = MAX_ORDER-1; ; )
    {
        i = find_next_zero_bit(&mask, BITS_PER_LONG, j + 1);
        if ( i >= BITS_PER_LONG )
            break;
        j = find_next_bit(&mask, BITS_PER_LONG, i + 1);
        if ( j >= BITS_PER_LONG )
            break;
        if ( j - i > hole_shift )
        {
            hole_shift = j - i;
            bottom_shift = i;
        }
    }
    if ( !hole_shift )
        return;

    printk(KERN_INFO "PFN compression on bits %u...%u\n",
           bottom_shift, bottom_shift + hole_shift - 1);

    pfn_pdx_hole_shift  = hole_shift;
    pfn_pdx_bottom_mask = (1UL << bottom_shift) - 1;
    ma_va_bottom_mask   = (PAGE_SIZE << bottom_shift) - 1;
    pfn_hole_mask       = ((1UL << hole_shift) - 1) << bottom_shift;
    pfn_top_mask        = ~(pfn_pdx_bottom_mask | pfn_hole_mask);
    ma_top_mask         = pfn_top_mask << PAGE_SHIFT;
}

#endif /* CONFIG_PDX_COMPRESSION */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: pdx.c === */
/* === BEGIN INLINED: pmap.c === */
#include <prtos_prtos_config.h>
#include <prtos_bitops.h>
#include <prtos_init.h>
#include <prtos_irq.h>
#include <prtos_pmap.h>

#include <asm_pmap.h>
#include <asm_fixmap.h>

/*
 * Simple mapping infrastructure to map / unmap pages in fixed map.
 * This is used to set the page table before the map domain page infrastructure
 * is initialized.
 *
 * This structure is not protected by any locks, so it must not be used after
 * smp bring-up.
 */

/* Bitmap to track which slot is used */
static __initdata DECLARE_BITMAP(inuse, NUM_FIX_PMAP);

void *__init pmap_map(mfn_t mfn)
{
    unsigned int idx;
    unsigned int slot;

    ASSERT(system_state < SYS_STATE_smp_boot);
    ASSERT(!in_irq());

    idx = find_first_zero_bit(inuse, NUM_FIX_PMAP);
    if ( idx == NUM_FIX_PMAP )
        panic("Out of PMAP slots\n");

    __set_bit(idx, inuse);

    slot = idx + FIX_PMAP_BEGIN;
    ASSERT(slot >= FIX_PMAP_BEGIN && slot <= FIX_PMAP_END);

    /*
     * We cannot use set_fixmap() here. We use PMAP when the domain map
     * page infrastructure is not yet initialized, so map_pages_to_prtos() called
     * by set_fixmap() needs to map pages on demand, which then calls pmap()
     * again, resulting in a loop. Modify the PTEs directly instead. The same
     * is true for pmap_unmap().
     */
    arch_pmap_map(slot, mfn);

    return fix_to_virt(slot);
}

void __init pmap_unmap(const void *p)
{
    unsigned int idx;
    unsigned int slot = virt_to_fix((unsigned long)p);

    ASSERT(system_state < SYS_STATE_smp_boot);
    ASSERT(slot >= FIX_PMAP_BEGIN && slot <= FIX_PMAP_END);
    ASSERT(!in_irq());

    idx = slot - FIX_PMAP_BEGIN;

    __clear_bit(idx, inuse);
    arch_pmap_unmap(slot);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: pmap.c === */
/* === BEGIN INLINED: vmap.c === */
#include <prtos_prtos_config.h>
#ifdef VMAP_VIRT_START
#include <prtos_bitmap.h>
#include <prtos_cache.h>
#include <prtos_init.h>
#include <prtos_mm.h>
#include <prtos_pfn.h>
#include <prtos_spinlock.h>
#include <prtos_types.h>
#include <prtos_vmap.h>
#include <asm_page.h>

static DEFINE_SPINLOCK(vm_lock);
static void *__read_mostly vm_base[VMAP_REGION_NR];
#define vm_bitmap(x) ((unsigned long *)vm_base[x])
/* highest allocated bit in the bitmap */
static unsigned int __read_mostly vm_top[VMAP_REGION_NR];
/* total number of bits in the bitmap */
static unsigned int __read_mostly vm_end[VMAP_REGION_NR];
/* lowest known clear bit in the bitmap */
static unsigned int vm_low[VMAP_REGION_NR];

void __init vm_init_type(enum vmap_region type, void *start, void *end)
{
    unsigned int i, nr;
    unsigned long va;

    ASSERT(!vm_base[type]);

    vm_base[type] = start;
    vm_end[type] = PFN_DOWN(end - start);
    vm_low[type]= PFN_UP((vm_end[type] + 7) / 8);
    nr = PFN_UP((vm_low[type] + 7) / 8);
    vm_top[type] = nr * PAGE_SIZE * 8;

    for ( i = 0, va = (unsigned long)vm_bitmap(type); i < nr; ++i, va += PAGE_SIZE )
    {
        mfn_t mfn;
        int rc;

        if ( system_state == SYS_STATE_early_boot )
            mfn = alloc_boot_pages(1, 1);
        else
        {
            struct page_info *pg = alloc_domheap_page(NULL, 0);

            BUG_ON(!pg);
            mfn = page_to_mfn(pg);
        }
        rc = map_pages_to_prtos(va, mfn, 1, PAGE_HYPERVISOR);
        BUG_ON(rc);

        clear_page((void *)va);
    }
    bitmap_fill(vm_bitmap(type), vm_low[type]);

    /* Populate page tables for the bitmap if necessary. */
    populate_pt_range(va, vm_low[type] - nr);
}

static void *vm_alloc(unsigned int nr, unsigned int align,
                      enum vmap_region t)
{
    unsigned int start, bit;

    if ( !align )
        align = 1;
    else if ( align & (align - 1) )
        align = ISOLATE_LSB(align);

    ASSERT((t >= VMAP_DEFAULT) && (t < VMAP_REGION_NR));
    if ( !vm_base[t] )
        return NULL;

    spin_lock(&vm_lock);
    for ( ; ; )
    {
        mfn_t mfn;

        ASSERT(vm_low[t] == vm_top[t] || !test_bit(vm_low[t], vm_bitmap(t)));
        for ( start = vm_low[t]; start < vm_top[t]; )
        {
            bit = find_next_bit(vm_bitmap(t), vm_top[t], start + 1);
            if ( bit > vm_top[t] )
                bit = vm_top[t];
            /*
             * Note that this skips the first bit, making the
             * corresponding page a guard one.
             */
            start = (start + align) & ~(align - 1);
            if ( bit < vm_top[t] )
            {
                if ( start + nr < bit )
                    break;
                start = find_next_zero_bit(vm_bitmap(t), vm_top[t], bit + 1);
            }
            else
            {
                if ( start + nr <= bit )
                    break;
                start = bit;
            }
        }

        if ( start < vm_top[t] )
            break;

        spin_unlock(&vm_lock);

        if ( vm_top[t] >= vm_end[t] )
            return NULL;

        if ( system_state == SYS_STATE_early_boot )
            mfn = alloc_boot_pages(1, 1);
        else
        {
            struct page_info *pg = alloc_domheap_page(NULL, 0);

            if ( !pg )
                return NULL;
            mfn = page_to_mfn(pg);
        }

        spin_lock(&vm_lock);

        if ( start >= vm_top[t] )
        {
            unsigned long va = (unsigned long)vm_bitmap(t) + vm_top[t] / 8;

            if ( !map_pages_to_prtos(va, mfn, 1, PAGE_HYPERVISOR) )
            {
                clear_page((void *)va);
                vm_top[t] += PAGE_SIZE * 8;
                if ( vm_top[t] > vm_end[t] )
                    vm_top[t] = vm_end[t];
                continue;
            }
        }

        if ( system_state == SYS_STATE_early_boot )
            init_boot_pages(mfn_to_maddr(mfn), mfn_to_maddr(mfn) + PAGE_SIZE);
        else
            free_domheap_page(mfn_to_page(mfn));

        if ( start >= vm_top[t] )
        {
            spin_unlock(&vm_lock);
            return NULL;
        }
    }

    for ( bit = start; bit < start + nr; ++bit )
        __set_bit(bit, vm_bitmap(t));
    if ( bit < vm_top[t] )
        ASSERT(!test_bit(bit, vm_bitmap(t)));
    else
        ASSERT(bit == vm_top[t]);
    if ( start <= vm_low[t] + 2 )
        vm_low[t] = bit;
    spin_unlock(&vm_lock);

    return vm_base[t] + start * PAGE_SIZE;
}

static unsigned int vm_index(const void *va, enum vmap_region type)
{
    unsigned long addr = (unsigned long)va & ~(PAGE_SIZE - 1);
    unsigned int idx;
    unsigned long start = (unsigned long)vm_base[type];

    if ( !start )
        return 0;

    if ( addr < start + (vm_end[type] / 8) ||
         addr >= start + vm_top[type] * PAGE_SIZE )
        return 0;

    idx = PFN_DOWN(va - vm_base[type]);
    return !test_bit(idx - 1, vm_bitmap(type)) &&
           test_bit(idx, vm_bitmap(type)) ? idx : 0;
}

static unsigned int vm_size(const void *va, enum vmap_region type)
{
    unsigned int start = vm_index(va, type), end;

    if ( !start )
        return 0;

    end = find_next_zero_bit(vm_bitmap(type), vm_top[type], start + 1);

    return min(end, vm_top[type]) - start;
}

static void vm_free(const void *va)
{
    enum vmap_region type = VMAP_DEFAULT;
    unsigned int bit = vm_index(va, type);

    if ( !bit )
    {
        type = VMAP_PRTOS;
        bit = vm_index(va, type);
    }

    if ( !bit )
    {
        WARN_ON(va != NULL);
        return;
    }

    spin_lock(&vm_lock);
    if ( bit < vm_low[type] )
    {
        vm_low[type] = bit - 1;
        while ( !test_bit(vm_low[type] - 1, vm_bitmap(type)) )
            --vm_low[type];
    }
    while ( __test_and_clear_bit(bit, vm_bitmap(type)) )
        if ( ++bit == vm_top[type] )
            break;
    spin_unlock(&vm_lock);
}

void *__vmap(const mfn_t *mfn, unsigned int granularity,
             unsigned int nr, unsigned int align, unsigned int flags,
             enum vmap_region type)
{
    void *va = vm_alloc(nr * granularity, align, type);
    unsigned long cur = (unsigned long)va;

    for ( ; va && nr--; ++mfn, cur += PAGE_SIZE * granularity )
    {
        if ( map_pages_to_prtos(cur, *mfn, granularity, flags) )
        {
            vunmap(va);
            va = NULL;
        }
    }

    return va;
}

void *vmap(const mfn_t *mfn, unsigned int nr)
{
    return __vmap(mfn, 1, nr, 1, PAGE_HYPERVISOR, VMAP_DEFAULT);
}


unsigned int vmap_size(const void *va)
{
    unsigned int pages = vm_size(va, VMAP_DEFAULT);

    if ( !pages )
        pages = vm_size(va, VMAP_PRTOS);

    return pages;
}

void vunmap(const void *va)
{
    unsigned long addr = (unsigned long)va;
    unsigned pages = vmap_size(va);

#ifndef _PAGE_NONE
    destroy_prtos_mappings(addr, addr + PAGE_SIZE * pages);
#else /* Avoid tearing down intermediate page tables. */
    map_pages_to_prtos(addr, INVALID_MFN, pages, _PAGE_NONE);
#endif
    vm_free(va);
}

static void *vmalloc_type(size_t size, enum vmap_region type)
{
    mfn_t *mfn;
    unsigned int i, pages = PFN_UP(size);
    struct page_info *pg;
    void *va;

    ASSERT(size);

    if ( PFN_DOWN(size) > pages )
        return NULL;

    mfn = xmalloc_array(mfn_t, pages);
    if ( mfn == NULL )
        return NULL;

    for ( i = 0; i < pages; i++ )
    {
        pg = alloc_domheap_page(NULL, 0);
        if ( pg == NULL )
            goto error;
        mfn[i] = page_to_mfn(pg);
    }

    va = __vmap(mfn, 1, pages, 1, PAGE_HYPERVISOR, type);
    if ( va == NULL )
        goto error;

    xfree(mfn);
    return va;

 error:
    while ( i-- )
        free_domheap_page(mfn_to_page(mfn[i]));
    xfree(mfn);
    return NULL;
}



void *vzalloc(size_t size)
{
    void *p = vmalloc_type(size, VMAP_DEFAULT);
    int i;

    if ( p == NULL )
        return NULL;

    for ( i = 0; i < size; i += PAGE_SIZE )
        clear_page(p + i);

    return p;
}

void vfree(void *va)
{
    unsigned int i, pages;
    struct page_info *pg;
    PAGE_LIST_HEAD(pg_list);

    if ( !va )
        return;

    pages = vmap_size(va);
    ASSERT(pages);

    for ( i = 0; i < pages; i++ )
    {
        struct page_info *page = vmap_to_page(va + i * PAGE_SIZE);

        ASSERT(page);
        page_list_add(page, &pg_list);
    }
    vunmap(va);

    while ( (pg = page_list_remove_head(&pg_list)) != NULL )
        free_domheap_page(pg);
}


void vm_init(void) {
    vm_init_type(VMAP_DEFAULT, (void *)VMAP_VIRT_START, arch_vmap_virt_end());
}

#endif

/* === END INLINED: vmap.c === */
/* === BEGIN INLINED: guestcopy.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
#include <prtos_domain_page.h>
#include <prtos_guest_access.h>
#include <prtos_lib.h>
#include <prtos_mm.h>
#include <prtos_sched.h>

#include <asm_current.h>

#define COPY_flush_dcache   (1U << 0)
#define COPY_from_guest     (0U << 1)
#define COPY_to_guest       (1U << 1)
#define COPY_ipa            (0U << 2)
#define COPY_linear         (1U << 2)

typedef union
{
    struct
    {
        struct vcpu *v;
    } gva;

    struct
    {
        struct domain *d;
    } gpa;
} copy_info_t;

#define GVA_INFO(vcpu) ((copy_info_t) { .gva = { vcpu } })
#define GPA_INFO(domain) ((copy_info_t) { .gpa = { domain } })

static struct page_info *translate_get_page(copy_info_t info, uint64_t addr,
                                            bool linear, bool write)
{
    p2m_type_t p2mt;
    struct page_info *page;

    if ( linear )
        return get_page_from_gva(info.gva.v, addr,
                                 write ? GV2M_WRITE : GV2M_READ);

    page = get_page_from_gfn(info.gpa.d, paddr_to_pfn(addr), &p2mt, P2M_ALLOC);

    if ( !page )
        return NULL;

    if ( !p2m_is_ram(p2mt) )
    {
        put_page(page);
        return NULL;
    }

    return page;
}

static unsigned long copy_guest(void *buf, uint64_t addr, unsigned int len,
                                copy_info_t info, unsigned int flags)
{
    /* XXX needs to handle faults */
    unsigned int offset = addr & ~PAGE_MASK;

    BUILD_BUG_ON((sizeof(addr)) < sizeof(vaddr_t));
    BUILD_BUG_ON((sizeof(addr)) < sizeof(paddr_t));

    while ( len )
    {
        void *p;
        unsigned int size = min(len, (unsigned int)PAGE_SIZE - offset);
        struct page_info *page;

        page = translate_get_page(info, addr, flags & COPY_linear,
                                  flags & COPY_to_guest);
        if ( page == NULL )
            return len;

        p = __map_domain_page(page);
        p += offset;
        if ( flags & COPY_to_guest )
        {
            /*
             * buf will be NULL when the caller request to zero the
             * guest memory.
             */
            if ( buf )
                memcpy(p, buf, size);
            else
                memset(p, 0, size);
        }
        else
            memcpy(buf, p, size);

        if ( flags & COPY_flush_dcache )
            clean_dcache_va_range(p, size);

        unmap_domain_page(p - offset);
        put_page(page);
        len -= size;
        buf += size;
        addr += size;
        /*
         * After the first iteration, guest virtual address is correctly
         * aligned to PAGE_SIZE.
         */
        offset = 0;
    }

    return 0;
}

unsigned long raw_copy_to_guest(void *to, const void *from, unsigned int len)
{
    return copy_guest((void *)from, (vaddr_t)to, len,
                      GVA_INFO(current), COPY_to_guest | COPY_linear);
}



unsigned long raw_copy_from_guest(void *to, const void __user *from,
                                  unsigned int len)
{
    return copy_guest(to, (vaddr_t)from, len, GVA_INFO(current),
                      COPY_from_guest | COPY_linear);
}

unsigned long copy_to_guest_phys_flush_dcache(struct domain *d,
                                              paddr_t gpa,
                                              void *buf,
                                              unsigned int len)
{
    return copy_guest(buf, gpa, len, GPA_INFO(d),
                      COPY_to_guest | COPY_ipa | COPY_flush_dcache);
}

int access_guest_memory_by_gpa(struct domain *d, paddr_t gpa, void *buf,
                               uint32_t size, bool is_write)
{
    unsigned long left;
    int flags = COPY_ipa;

    flags |= is_write ? COPY_to_guest : COPY_from_guest;

    left = copy_guest(buf, gpa, size, GPA_INFO(d), flags);

    return (!left) ? 0 : -EINVAL;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: guestcopy.c === */
/* === BEGIN INLINED: common_guestcopy.c === */
#include <prtos_prtos_config.h>
#include <prtos_lib.h>
#include <prtos_guest_access.h>
#include <prtos_err.h>

/*
 * The function copies a string from the guest and adds a NUL to
 * make sure the string is correctly terminated.
 */
char *safe_copy_string_from_guest(PRTOS_GUEST_HANDLE(char) u_buf,
                                  size_t size, size_t max_size)
{
    char *tmp;

    if ( size > max_size )
        return ERR_PTR(-ENOBUFS);

    /* Add an extra +1 to append \0 */
    tmp = xmalloc_array(char, size + 1);
    if ( !tmp )
        return ERR_PTR(-ENOMEM);

    if ( copy_from_guest(tmp, u_buf, size) )
    {
        xfree(tmp);
        return ERR_PTR(-EFAULT);
    }
    tmp[size] = '\0';

    return tmp;
}

/* === END INLINED: common_guestcopy.c === */
