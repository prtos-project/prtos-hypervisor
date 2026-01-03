/*
 * FILE: physmm.h
 *
 * Physical memory manager
 *
 * www.prtos.org
 */

#ifndef _PRTOS_PHYSMM_H_
#define _PRTOS_PHYSMM_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <kthread.h>
#include <processor.h>
#include <hypercalls.h>
#include <arch/paging.h>
#include <arch/physmm.h>

#ifdef CONFIG_MMU
/*
 * Structure representing a physical page in the physical memory manager
 * It contains information about the page, such as its virtual address, mapping status,
 * unlock status, type, and a counter for reference counting
 * It also includes a spin lock for synchronization
 * The structure is used to manage physical pages in the system, allowing for operations such as
 * incrementing and decrementing the reference count, mapping and unlocking pages, and finding pages
 * in the physical memory manager
 */
struct phys_page {
    struct dyn_list_node list_node;
    prtos_address_t v_addr;
    prtos_u32_t mapped : 1, unlocked : 1, type : 3, counter : 27;
    spin_lock_t lock;
};

#else
struct phys_page {};
#endif

static inline void phys_page_inc_counter(struct phys_page *page) {
    prtos_u32_t cnt;

    spin_lock(&page->lock);
    cnt = page->counter;
    page->counter++;
    spin_unlock(&page->lock);

    if (cnt == ~0) {
        cpu_ctxt_t ctxt;
        get_cpu_ctxt(&ctxt);
        part_panic(&ctxt, __PRTOS_FILE__ ":%u: counter overflow", __LINE__);
    }
}

static inline void phys_page_dec_counter(struct phys_page *page) {
    prtos_u32_t cnt;

    spin_lock(&page->lock);
    cnt = page->counter;
    page->counter--;
    spin_unlock(&page->lock);

    if (!cnt) {
        cpu_ctxt_t ctxt;
        get_cpu_ctxt(&ctxt);
        part_panic(&ctxt, __PRTOS_FILE__ ":%u: counter underflow", __LINE__);
    }
}

extern void setup_phys_mm(void);
extern struct phys_page *pmm_find_page(prtos_address_t p_addr, partition_t *p, prtos_u32_t *flags);
extern struct phys_page *phys_mm_find_anonymous_page(prtos_address_t p_addr);
extern prtos_s32_t phys_mm_find_addr(prtos_address_t p_addr, partition_t *p, prtos_u32_t *flags);
extern prtos_s32_t phys_mm_find_area(prtos_address_t p_addr, prtos_s_size_t size, partition_t *k, prtos_u32_t *flags);
extern void phys_mm_reset_part(partition_t *p);
extern void *vcache_map_page(prtos_address_t p_addr, struct phys_page *page);
extern void vcache_unlock_page(struct phys_page *page);
extern prtos_address_t enable_by_pass_mmu(prtos_address_t addr, partition_t *p, struct phys_page **);
extern inline void disable_by_pass_mmu(struct phys_page *);

#endif
