/*
 * FILE: vmmap.c
 *
 * Virtual memory map
 *
 * www.prtos.org
 */

#include <assert.h>
#include <kthread.h>
#include <physmm.h>
#include <rsvmem.h>
#include <sched.h>
#include <vmmap.h>
#include <arch/physmm.h>
#include <arch/paging.h>

static inline prtos_address_t vaddr_to_paddr(struct prtos_conf_memory_area *memory_areas, prtos_s32_t num_of_areas, prtos_address_t v_addr) {
    prtos_s32_t e;
    for (e = 0; e < num_of_areas; e++)
        if ((memory_areas[e].mapped_at <= v_addr) && (((memory_areas[e].mapped_at + memory_areas[e].size) - 1) >= v_addr))
            return v_addr - memory_areas[e].mapped_at + memory_areas[e].start_addr;
    return -1;
}

// Alloc memory from memory pool specified by \p *pool and \p *max_size, this memory comes form partion's memory for partition page directory and page table
static prtos_address_t alloc_mem(struct prtos_conf_part *cfg, prtos_u_size_t size, prtos_u32_t align, prtos_address_t *pool, prtos_s_size_t *max_size) {
    prtos_address_t addr;
    prtos_s32_t e;

    if (*pool & (align - 1)) {
        *max_size -= align - (*pool & (align - 1));
        *pool = align + (*pool & ~(align - 1));
    }

    addr = *pool;
    *pool += size;
    if ((*max_size -= size) < 0) {
        kprintf("[setup_page_table] partition page table couldn't be created\n");
        return ~0;
    }

    for (e = 0; e < size; e += sizeof(prtos_u32_t)) {
        write_by_pass_mmu_word((void *)(addr + e), 0);
    }

    addr = vaddr_to_paddr(&prtos_conf_phys_mem_area_table[cfg->physical_memory_areas_offset], cfg->num_of_physical_memory_areas, addr);
    return addr;
}

static inline int setup_pbl(partition_t *p, prtos_word_t *p_ptd_level_1_table, prtos_address_t at, prtos_address_t page_table, prtos_u_size_t size) {
    extern prtos_u8_t _spbl[], _epbl[];
    prtos_address_t addr, v_addr = 0, a, b;
    // struct phys_page *page;
    void *stack;
    // prtos_word_t attr;
    prtos_s32_t i;

    ASSERT(((prtos_address_t)_epbl - (prtos_address_t)_spbl) <= 256 * 1024);
    ASSERT(prtos_conf_boot_partition_table[p->cfg->id].num_of_custom_files <= CONFIG_MAX_NO_CUSTOMFILES);

    /*Partition Loader Stack*/
    if (!(p->pbl_stack)) {
        GET_MEMA(stack, 18 * PAGE_SIZE, PAGE_SIZE);
        p->pbl_stack = (prtos_address_t)stack;
    } else
        stack = (void *)p->pbl_stack;

    a = _VIRT2PHYS(stack);
    b = a + (18 * PAGE_SIZE) - 1;
    v_addr = at - 18 * PAGE_SIZE;

    for (addr = a; (addr >= a) && (addr < b); addr += PAGE_SIZE, v_addr += PAGE_SIZE) {
        if (vm_map_user_page(p, p_ptd_level_1_table, addr, v_addr, _PG_ATTR_PRESENT | _PG_ATTR_USER | _PG_ATTR_CACHED | _PG_ATTR_RW, alloc_mem, &page_table,
                             &size) < 0) {
            kprintf("[setup_pbl(P%d)] Error mapping the Partition Loader Stack\n", p->cfg->id);
            return -1;
        }
    }

    /*Partition Loader code*/
    a = (prtos_address_t)_spbl;
    b = a + (256 * 1024) - 1;
    v_addr = at;
    for (addr = a; (addr >= a) && (addr < b); addr += PAGE_SIZE, v_addr += PAGE_SIZE) {
        if (vm_map_user_page(p, p_ptd_level_1_table, addr, v_addr, _PG_ATTR_PRESENT | _PG_ATTR_USER | _PG_ATTR_CACHED, alloc_mem, &page_table, &size) < 0) {
            kprintf("[setup_pbl(P%d)] Error mapping the Partition Loader Code\n", p->cfg->id);
            return -1;
        }
    }

    /*Mapping partition image from container*/
    a = prtos_conf_boot_partition_table[p->cfg->id].image_start;
    b = a + (prtos_conf_boot_partition_table[p->cfg->id].img_size) - 1;
    v_addr = a;
    p->image_start = v_addr;

    for (addr = a; (addr >= a) && (addr < b); addr += PAGE_SIZE, v_addr += PAGE_SIZE) {
        if (vm_map_user_page(p, p_ptd_level_1_table, addr, v_addr, _PG_ATTR_PRESENT | _PG_ATTR_USER | _PG_ATTR_CACHED, alloc_mem, &page_table, &size) < 0) {
            kprintf("[setup_pbl(P%d)] Error mapping the Partition image from container\n", p->cfg->id);
            return -1;
        }
    }

    for (i = 0; i < prtos_conf_boot_partition_table[p->cfg->id].num_of_custom_files; i++) {
        a = prtos_conf_boot_partition_table[p->cfg->id].custom_file_table[i].start_addr;
        b = a + prtos_conf_boot_partition_table[p->cfg->id].custom_file_table[i].size - 1;

        for (addr = a; (addr >= a) && (addr < b); addr += PAGE_SIZE, v_addr += PAGE_SIZE) {
            if (vm_map_user_page(p, p_ptd_level_1_table, addr, v_addr, _PG_ATTR_PRESENT | _PG_ATTR_USER | _PG_ATTR_CACHED, alloc_mem, &page_table, &size) < 0) {
                return -1;
            }
        }
    }

    return 0;
}

prtos_address_t setup_page_table(partition_t *p, prtos_address_t page_table, prtos_u_size_t size) {
    prtos_address_t addr, v_addr = 0, a, b, pt;
    prtos_word_t *p_ptd_level_1_table, attr;
    struct phys_page *ptd_level_1_table, *page;
    prtos_s32_t e;

    if ((pt = alloc_mem(p->cfg, PTDL1SIZE, PTDL1SIZE, &page_table, &size)) == ~0) {
        PWARN("(%d) Unable to create page table (out of memory)\n", p->cfg->id);
        return ~0;
    }

    if (!(ptd_level_1_table = pmm_find_page(pt, p, 0))) {
        PWARN("(%d) Page 0x%x does not belong to this partition\n", p->cfg->id, pt);
        return ~0;
    }

    ptd_level_1_table->type = PPAG_PTDL1;

    // Incremented because it is load as the initial page table
    phys_page_inc_counter(ptd_level_1_table);
    p_ptd_level_1_table = vcache_map_page(pt, ptd_level_1_table);
    ASSERT(PTDL1SIZE <= PAGE_SIZE);
    for (e = 0; e < PTDL1ENTRIES; e++) write_by_pass_mmu_word(&p_ptd_level_1_table[e], 0);

    for (e = 0; e < p->cfg->num_of_physical_memory_areas; e++) {
        if (prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].flags & PRTOS_MEM_AREA_UNMAPPED) continue;

        a = prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].start_addr;
        b = a + prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].size - 1;
        v_addr = prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].mapped_at;
        for (addr = a; (addr >= a) && (addr < b); addr += PAGE_SIZE, v_addr += PAGE_SIZE) {
            attr = _PG_ATTR_PRESENT | _PG_ATTR_USER;

            if (!(prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].flags & PRTOS_MEM_AREA_UNCACHEABLE)) attr |= _PG_ATTR_CACHED;

            if (!(prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].flags & PRTOS_MEM_AREA_READONLY)) attr |= _PG_ATTR_RW;

            if (vm_map_user_page(p, p_ptd_level_1_table, addr, v_addr, attr, alloc_mem, &page_table, &size) < 0) return ~0;
        }
    }

    attr = _PG_ATTR_PRESENT | _PG_ATTR_USER;
    ASSERT(p->pct_array_size);

    for (v_addr = PRTOS_PCTRLTAB_ADDR, addr = (prtos_address_t)_VIRT2PHYS(p->pct_array); addr < ((prtos_address_t)_VIRT2PHYS(p->pct_array) + p->pct_array_size);
         addr += PAGE_SIZE, v_addr += PAGE_SIZE) {
        if (vm_map_user_page(p, p_ptd_level_1_table, addr, v_addr, attr, alloc_mem, &page_table, &size) < 0) return ~0;
    }

    // This is a hard coded partition memory space special for partition loader
    prtos_address_t v_addr_loader = PRTOS_PCTRLTAB_ADDR - 256 * 1024;
    if (setup_pbl(p, p_ptd_level_1_table, v_addr_loader, page_table, size) < 0) return ~0;

    attr = _PG_ATTR_PRESENT | _PG_ATTR_USER;
    // Set appropriate permissions for the page table entry which used to put partition page directory table and page table.
    for (e = 0; e < p->cfg->num_of_physical_memory_areas; e++) {
        if (prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].flags & PRTOS_MEM_AREA_UNMAPPED) continue;

        a = prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].start_addr;
        b = a + prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].size - 1;
        v_addr = prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].mapped_at;
        for (addr = a; (addr >= a) && (addr < b); addr += PAGE_SIZE, v_addr += PAGE_SIZE) {
            if ((page = pmm_find_page(addr, p, 0))) {
                phys_page_inc_counter(page);
                if (prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset].flags & PRTOS_MEM_AREA_UNCACHEABLE)
                    attr &= ~_PG_ATTR_CACHED;
                else
                    attr |= _PG_ATTR_CACHED;

                if (page->type != PPAG_STD) {
                    if (vm_map_user_page(p, p_ptd_level_1_table, addr, v_addr, attr, alloc_mem, &page_table, &size) < 0) return ~0;
                }
            }
        }
    }

    vcache_unlock_page(ptd_level_1_table);

    return pt;
}
