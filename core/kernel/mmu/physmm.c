/*
 * FILE: physmm.c
 *
 * Physical memory manager
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <list.h>
#include <rsvmem.h>
#include <physmm.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>
#include <virtmm.h>
#include <vmmap.h>
#include <arch/paging.h>
#include <arch/physmm.h>
#include <arch/prtos_def.h>

static struct dyn_list cache_lru;
static struct phys_page **phys_page_table;

struct phys_page *phis_mm_find_anonymous_page(prtos_address_t p_addr) {
    prtos_s32_t l, r, c;
    prtos_address_t a, b;

    p_addr &= PAGE_MASK;
    for (l = 0, r = prtos_conf_table.num_of_regions - 1; l <= r;) {
        c = (l + r) >> 1;
        a = prtos_conf_mem_reg_table[c].start_addr;
        b = a + prtos_conf_mem_reg_table[c].size - 1;
        if (p_addr < a) {
            r = c - 1;
        } else {
            if (p_addr > b) {
                l = c + 1;
            } else {
                ASSERT((p_addr >= a) && ((p_addr + PAGE_SIZE - 1) <= b));
                if (!(prtos_conf_mem_reg_table[c].flags & PRTOSC_REG_FLAG_PGTAB)) return 0;

                return &phys_page_table[c][(p_addr - a) >> PAGE_SHIFT];
            }
        }
    }

#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_VMM_MODULE, AUDIT_VMM_PPG_DOESNT_EXIST, 1, (prtos_word_t *)&p_addr);
#endif

    return 0;
}

struct phys_page *pmm_find_page(prtos_address_t p_addr, partition_t *p, prtos_u32_t *flags) {
    struct prtos_conf_memory_area *mem_area;
    struct prtos_conf_part *cfg;
    prtos_s32_t l, r, c;
    prtos_address_t a, b;
    ASSERT(p);
    p_addr &= PAGE_MASK;
    cfg = p->cfg;
    for (l = 0, r = cfg->num_of_physical_memory_areas - 1; l <= r;) {
        c = (l + r) >> 1;
        mem_area = &prtos_conf_phys_mem_area_table[c + cfg->physical_memory_areas_offset];
        a = mem_area->start_addr;
        b = a + mem_area->size - 1;
        if (p_addr < a) {
            r = c - 1;
        } else {
            if (p_addr > b) {
                l = c + 1;
            } else {
                struct prtos_conf_memory_region *mem_region = &prtos_conf_mem_reg_table[mem_area->memory_region_offset];
                ASSERT((p_addr >= a) && ((p_addr + PAGE_SIZE - 1) <= b));
                if (!(mem_region->flags & PRTOSC_REG_FLAG_PGTAB)) return 0;
                if (flags) *flags = mem_area->flags;

                return &phys_page_table[mem_area->memory_region_offset][(p_addr - mem_region->start_addr) >> PAGE_SHIFT];
            }
        }
    }

#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_VMM_MODULE, AUDIT_VMM_PPG_DOESNT_BELONG, 1, (prtos_word_t *)&p_addr);
#endif

    return 0;
}

prtos_s32_t phys_mm_find_addr(prtos_address_t p_addr, partition_t *p, prtos_u32_t *flags) {
    struct prtos_conf_memory_area *mem_area;
    struct prtos_conf_part *cfg;
    prtos_s32_t l, r, c;
    prtos_address_t a, b;

    p_addr &= PAGE_MASK;
    cfg = p->cfg;

    for (l = 0, r = cfg->num_of_physical_memory_areas - 1; l <= r;) {
        c = (l + r) >> 1;
        mem_area = &prtos_conf_phys_mem_area_table[c + cfg->physical_memory_areas_offset];
        a = mem_area->start_addr;
        b = a + mem_area->size - 1;
        if (p_addr < a) {
            r = c - 1;
        } else {
            if (p_addr > b) {
                l = c + 1;
            } else {
                ASSERT((p_addr >= a) && ((p_addr + PAGE_SIZE - 1) <= b));
                if (flags) *flags = mem_area->flags;
                return 1;
            }
        }
    }

    PWARN("Page 0x%x does not belong to %d\n", p_addr, cfg->id);
#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_VMM_MODULE, AUDIT_VMM_PPG_DOESNT_BELONG, 1, (prtos_word_t *)&p_addr);
#endif

    return 0;
}

prtos_s32_t phys_mm_find_area(prtos_address_t p_addr, prtos_s_size_t size, partition_t *p, prtos_u32_t *flags) {
    struct prtos_conf_memory_area *mem_area;
    struct prtos_conf_part *cfg;
    prtos_s32_t l, r, c;
    prtos_address_t a, b;

    if (p) {
        cfg = p->cfg;

        for (l = 0, r = cfg->num_of_physical_memory_areas - 1; l <= r;) {
            c = (l + r) >> 1;
            mem_area = &prtos_conf_phys_mem_area_table[c + cfg->physical_memory_areas_offset];
            a = mem_area->start_addr;
            b = a + mem_area->size - 1;
            if (p_addr < a) {
                r = c - 1;
            } else {
                if ((p_addr + size - 1) > b) {
                    l = c + 1;
                } else {
                    ASSERT((p_addr >= a) && ((p_addr + size - 1) <= b));
                    if (flags) *flags = mem_area->flags;
                    return 1;
                }
            }
        }
    } else {
        for (l = 0, r = prtos_conf_table.num_of_regions - 1; l <= r;) {
            c = (l + r) >> 1;
            a = prtos_conf_mem_reg_table[c].start_addr;
            b = a + prtos_conf_mem_reg_table[c].size - 1;
            if (p_addr < a) {
                r = c - 1;
            } else {
                if ((p_addr + size - 1) > b) {
                    l = c + 1;
                } else {
                    ASSERT((p_addr >= a) && ((p_addr + size - 1) <= b));
                    if (flags) *flags = prtos_conf_mem_reg_table[c].flags;
                    return 1;
                }
            }
        }
    }

    return 0;
}

void phys_mm_reset_part(partition_t *p) {
    struct prtos_conf_memory_region *mem_region;
    struct prtos_conf_memory_area *mem_area;
    struct phys_page *page;
    prtos_address_t addr;
    prtos_s32_t e;

    for (e = 0; e < p->cfg->num_of_physical_memory_areas; e++) {
        mem_area = &prtos_conf_phys_mem_area_table[e + p->cfg->physical_memory_areas_offset];
        mem_region = &prtos_conf_mem_reg_table[mem_area->memory_region_offset];
        if (mem_region->flags & PRTOSC_REG_FLAG_PGTAB)
            for (addr = mem_area->start_addr; addr < mem_area->start_addr + mem_area->size; addr += PAGE_SIZE) {
                page = &phys_page_table[mem_area->memory_region_offset][(addr - mem_region->start_addr) >> PAGE_SHIFT];
                spin_lock(&page->lock);
                page->type = PPAG_STD;
                page->counter = 0;
                spin_unlock(&page->lock);

                if (page->mapped) vcache_unlock_page(page);
            }
    }
}

//#ifdef CONFIG_SMP
//#error "not SMP safe"
//#endif

void *vcache_map_page(prtos_address_t p_addr, struct phys_page *page) {
    if (page->mapped) return (void *)(page->v_addr + (p_addr & (PAGE_SIZE - 1)));

    if (vmm_get_num_of_free_frames() <= 0) {
        struct phys_page *victim_page;
        // Unmapping the last mapped page
        victim_page = dyn_list_remove_tail(&cache_lru);
        ASSERT(victim_page);
        victim_page->unlocked = 0;
        victim_page->mapped = 0;
        page->v_addr = victim_page->v_addr;
        victim_page->v_addr = ~0;
    } else
        page->v_addr = vmm_alloc(1);

    page->mapped = 1;
    vm_map_page(p_addr & PAGE_MASK, page->v_addr, _PG_ATTR_PRESENT | _PG_ATTR_RW | _PG_ATTR_CACHED);
    return (void *)(page->v_addr + (p_addr & (PAGE_SIZE - 1)));
}

void vcache_unlock_page(struct phys_page *page) {
    ASSERT(page && page->mapped);
    if (!page->unlocked) {
        page->unlocked = 1;
        dyn_list_insert_head(&cache_lru, &page->list_node);
    }
}

void setup_phys_mm(void) {
    prtos_s32_t e, i;

    dyn_list_init(&cache_lru);
    GET_MEMZ(phys_page_table, sizeof(struct phys_page *) * prtos_conf_table.num_of_regions);
    for (e = 0; e < prtos_conf_table.num_of_regions; e++) {
        ASSERT(!(prtos_conf_mem_reg_table[e].size & (PAGE_SIZE - 1)) && !(prtos_conf_mem_reg_table[e].start_addr & (PAGE_SIZE - 1)));
        if (prtos_conf_mem_reg_table[e].flags & PRTOSC_REG_FLAG_PGTAB) {
            GET_MEMZ(phys_page_table[e], sizeof(struct phys_page) * (prtos_conf_mem_reg_table[e].size / PAGE_SIZE));
            for (i = 0; i < prtos_conf_mem_reg_table[e].size / PAGE_SIZE; i++) phys_page_table[e][i].lock = SPINLOCK_INIT;
        } else {
            phys_page_table[e] = 0;
        }
    }
}
