/*
 * FILE: hypercalls.c
 *
 * prtos's hypercalls
 *
 * www.prtos.org
 */

#include <assert.h>
#include <kthread.h>
#include <gaccess.h>
#include <physmm.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>
#include <hypercalls.h>
#include <virtmm.h>
#include <vmmap.h>
#include <arch/physmm.h>

#ifdef CONFIG_VMM_UPDATE_HYPERCALLS
static prtos_s32_t update_ptd(struct phys_page *page_ptd, prtos_address_t p_addr, prtos_address_t *val) {
    prtos_word_t *vptd = vcache_map_page(p_addr, page_ptd), old_val;
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct phys_page *page;

    if (!IS_VALID_PTD_PTR(page_ptd->type, p_addr)) return PRTOS_INVALID_PARAM;

    old_val = read_by_pass_mmu_word(vptd);
    vcache_unlock_page(page_ptd);

    if (IS_PTD_PRESENT(*val)) {
        if (!(page = pmm_find_page(GET_PTD_ADDR(*val), get_partition(info->sched.current_kthread), 0))) {
#ifdef CONFIG_AUDIT_EVENTS
            raise_audit_event(TRACE_VMM_MODULE, AUDIT_VMM_INVLD_PTDE, 1, (prtos_word_t *)val);
#endif
            return PRTOS_INVALID_PARAM;
        }

        if (!IS_VALID_PTD_ENTRY(page->type)) {
#ifdef CONFIG_AUDIT_EVENTS
            raise_audit_event(TRACE_VMM_MODULE, AUDIT_VMM_INVLD_PTDE, 1, (prtos_word_t *)val);
#endif
            return PRTOS_INVALID_PARAM;
        }
        phys_page_inc_counter(page);
    }

    if (IS_PTD_PRESENT(old_val)) {
        if (!(page = pmm_find_page(GET_PTD_ADDR(old_val), get_partition(info->sched.current_kthread), 0))) {
            return PRTOS_INVALID_PARAM;
        }
        ASSERT(IS_VALID_PTD_ENTRY(page->type));
        ASSERT(page->counter > 0);
        phys_page_dec_counter(page);
    }

    return PRTOS_OK;
}

static prtos_s32_t update_pte(struct phys_page *page_pte, prtos_address_t p_addr, prtos_address_t *val) {
    prtos_word_t *vpte = vcache_map_page(p_addr, page_pte), old_val;
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    struct phys_page *page;
    prtos_u32_t area_flags, attr;
    prtos_s32_t is_p_ctrl_table = 0;

    old_val = read_by_pass_mmu_word(vpte);
    vcache_unlock_page(page_pte);
    if (IS_PTE_PRESENT(*val)) {
        if (!(page = pmm_find_page(GET_PTE_ADDR(*val), get_partition(k), &area_flags)))
            if (!(is_p_ctrl_table = is_ptr_in_ctrl_table_page(GET_PTE_ADDR(*val), k->ctrl.g)))
                if (!phys_mm_find_addr(GET_PTE_ADDR(*val), get_partition(k), &area_flags)) {
                    return PRTOS_INVALID_PARAM;
                }

        attr = vm_arch_attr_to_attr(*val);

        if (area_flags & PRTOS_MEM_AREA_READONLY) attr &= ~_PG_ATTR_RW;

        if (area_flags & PRTOS_MEM_AREA_UNCACHEABLE) attr &= ~_PG_ATTR_CACHED;

        if (page) {
            if (page->type != PPAG_STD) attr &= ~_PG_ATTR_RW;
            phys_page_inc_counter(page);
        } else if (is_p_ctrl_table)
            attr &= ~_PG_ATTR_RW;
        *val = (*val & _PG_ARCH_ADDR) | vm_attr_to_arch_attr(attr);
    }

    if (IS_PTE_PRESENT(old_val)) {
        if (!(page = pmm_find_page(GET_PTE_ADDR(old_val), get_partition(k), 0))) {
            if (!is_ptr_in_ctrl_table_page(GET_PTE_ADDR(old_val), k->ctrl.g))
                if (!phys_mm_find_addr(GET_PTE_ADDR(old_val), get_partition(k), 0)) {
                    return PRTOS_INVALID_PARAM;
                }
        }
        if (page) {
            ASSERT(page->counter > 0);
            phys_page_dec_counter(page);
        }
    }

    return PRTOS_OK;
}

static void unset_ptd(prtos_address_t p_addr, struct phys_page *page_ptd, prtos_u32_t type) {
    prtos_word_t *vptd = vcache_map_page(p_addr, page_ptd);
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct phys_page *page;
    prtos_s32_t e;

    for (e = 0; e < GET_USER_PTD_ENTRIES(type); e++) {
        prtos_word_t vptd_val = read_by_pass_mmu_word(&vptd[e]);
        if (IS_PTD_PRESENT(vptd_val)) {
            if (!(page = pmm_find_page(GET_PTD_ADDR(vptd_val), get_partition(info->sched.current_kthread), 0))) return;
            ASSERT(IS_VALID_PTD_ENTRY(page->type));
            ASSERT(page->counter > 0);
            phys_page_dec_counter(page);
        }
    }
    vcache_unlock_page(page_ptd);
}

static void unset_pte(prtos_address_t p_addr, struct phys_page *page_pte, prtos_u32_t type) {
    prtos_word_t *vpte = vcache_map_page(p_addr, page_pte);
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct phys_page *page;
    prtos_s32_t e;

    for (e = 0; e < GET_USER_PTE_ENTRIES(type); e++) {
        prtos_word_t vpte_val = read_by_pass_mmu_word(&vpte[e]);
        if (IS_PTE_PRESENT(vpte_val)) {
            if (!(page = pmm_find_page(GET_PTE_ADDR(vpte_val), get_partition(info->sched.current_kthread), 0))) return;
            ASSERT(IS_VALID_PTE_ENTRY(page->type));
            ASSERT(page->counter > 0);
            phys_page_dec_counter(page);
        }
    }
    vcache_unlock_page(page_pte);
}

static void set_ptd(prtos_address_t p_addr, struct phys_page *page_ptd, prtos_u32_t type) {
    prtos_word_t *vptd = vcache_map_page(p_addr, page_ptd);
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct phys_page *page;
    prtos_s32_t e;

    for (e = 0; e < GET_USER_PTD_ENTRIES(type); e++) {
        prtos_word_t vptd_val = read_by_pass_mmu_word(&vptd[e]);
        if (IS_PTD_PRESENT(vptd_val)) {
            if (!(page = pmm_find_page(GET_PTD_ADDR(vptd_val), get_partition(info->sched.current_kthread), 0))) return;

            if (!IS_VALID_PTD_ENTRY(page->type)) {
#ifdef CONFIG_AUDIT_EVENTS
                raise_audit_event(TRACE_VMM_MODULE, AUDIT_VMM_INVLD_PTDE, 1, (prtos_word_t *)&vptd_val);
#endif
                write_by_pass_mmu_word(&vptd[e], SET_PTD_NOT_PRESENT(vptd_val));
                continue;
            }
            phys_page_inc_counter(page);
        }
    }
    CLONE_PRTOS_PTD_ENTRIES(type, vptd);

    vcache_unlock_page(page_ptd);
}

static void set_pte(prtos_address_t p_addr, struct phys_page *page_pte, prtos_u32_t type) {
    prtos_word_t *vpte = vcache_map_page(p_addr, page_pte);
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct phys_page *page;
    prtos_u32_t area_flags;
    prtos_s32_t e;

    for (e = 0; e < GET_USER_PTE_ENTRIES(type); e++) {
        prtos_word_t vpte_val = read_by_pass_mmu_word(&vpte[e]);
        if (IS_PTE_PRESENT(vpte_val)) {
            if (!(page = pmm_find_page(GET_PTE_ADDR(vpte_val), get_partition(info->sched.current_kthread), &area_flags)))
                if (!phys_mm_find_addr(GET_PTE_ADDR(vpte_val), get_partition(info->sched.current_kthread), &area_flags)) {
                    return;
                }
            if (area_flags & PRTOS_MEM_AREA_READONLY) vpte_val = SET_PTE_RONLY(vpte_val);

            if (area_flags & PRTOS_MEM_AREA_UNCACHEABLE) vpte_val = SET_PTE_UNCACHED(vpte_val);

            if (page) {
                if (page->type != PPAG_STD) vpte_val = SET_PTE_RONLY(vpte_val);
                phys_page_inc_counter(page);
            }
            write_by_pass_mmu_word(&vpte[e], vpte_val);
        }
    }
    vcache_unlock_page(page_pte);
}

#endif

static prtos_s32_t (*const update_phys_pag32_handle_table[NR_PPAG])(struct phys_page *, prtos_address_t, prtos_address_t *) = {
#ifdef CONFIG_VMM_UPDATE_HYPERCALLS
    [PPAG_STD] = 0,
    [PPAG_PTDL1] = update_ptd,
#if PTD_LEVELS > 2
    [PPAG_PTDL2] = update_ptd,
    [PPAG_PTDL3] = update_pte,
#else
    [PPAG_PTDL2] = update_pte,
#endif
#else
    [PPAG_STD] = 0,
    [PPAG_PTDL1] = 0,
    [PPAG_PTDL2] = 0,
#if PTD_LEVELS > 2
    [PPAG_PTDL3] = 0,
#endif
#endif
};

static void (*const unset_phys_pagtype_handle_table[NR_PPAG])(prtos_address_t, struct phys_page *, prtos_u32_t) = {
#ifdef CONFIG_VMM_UPDATE_HYPERCALLS
    [PPAG_STD] = 0,
    [PPAG_PTDL1] = unset_ptd,
#if PTD_LEVELS > 2
    [PPAG_PTDL2] = unset_ptd,
    [PPAG_PTDL3] = unset_pte,
#else
    [PPAG_PTDL2] = unset_pte,
#endif
#else
    [PPAG_STD] = 0,
    [PPAG_PTDL1] = 0,
    [PPAG_PTDL2] = 0,
#if PTD_LEVELS > 2
    [PPAG_PTDL3] = 0,
#endif
#endif
};

static void (*const set_phys_pagtype_handle_table[NR_PPAG])(prtos_address_t, struct phys_page *, prtos_u32_t) = {
#ifdef CONFIG_VMM_UPDATE_HYPERCALLS
    [PPAG_STD] = 0,
    [PPAG_PTDL1] = set_ptd,
#if PTD_LEVELS > 2
    [PPAG_PTDL2] = set_ptd,
    [PPAG_PTDL3] = set_pte,
#else
    [PPAG_PTDL2] = set_pte,
#endif
#else
    [PPAG_STD] = 0,
    [PPAG_PTDL1] = 0,
    [PPAG_PTDL2] = 0,
#if PTD_LEVELS > 2
    [PPAG_PTDL3] = 0,
#endif
#endif
};

__hypercall prtos_s32_t update_page32_sys(prtos_address_t p_addr, prtos_u32_t val) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct phys_page *page;
    prtos_u32_t addr;

    ASSERT(!hw_is_sti());
    if (p_addr & 3) return PRTOS_INVALID_PARAM;

    if (!(page = pmm_find_page(p_addr, get_partition(info->sched.current_kthread), 0))) {
        return PRTOS_INVALID_PARAM;
    }

    if (update_phys_pag32_handle_table[page->type])
        if (update_phys_pag32_handle_table[page->type](page, p_addr, &val) < 0) return PRTOS_INVALID_PARAM;

    addr = (prtos_address_t)vcache_map_page(p_addr, page);
    write_by_pass_mmu_word((void *)addr, val);
    vcache_unlock_page(page);
    return PRTOS_OK;
}

__hypercall prtos_s32_t set_page_type_sys(prtos_address_t p_addr, prtos_u32_t type) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct phys_page *page;

    ASSERT(!hw_is_sti());
    if (type >= NR_PPAG) return PRTOS_INVALID_PARAM;

    if (!(page = pmm_find_page(p_addr, get_partition(info->sched.current_kthread), 0))) return PRTOS_INVALID_PARAM;

    if (type != page->type) {
        if (page->counter) {
#ifdef CONFIG_AUDIT_EVENTS
            prtos_word_t audit_args[2];
            audit_args[0] = p_addr;
            audit_args[1] = page->counter;
            raise_audit_event(TRACE_VMM_MODULE, AUDIT_VMM_PPG_CNT, 2, audit_args);
#endif
            return PRTOS_OP_NOT_ALLOWED;
        }
        if (unset_phys_pagtype_handle_table[page->type]) unset_phys_pagtype_handle_table[page->type](p_addr, page, type);

        if (set_phys_pagtype_handle_table[type]) set_phys_pagtype_handle_table[type](p_addr, page, type);

        page->type = type;
        return PRTOS_OK;
    }

    return PRTOS_NO_ACTION;
}

__hypercall prtos_s32_t invald_tlb_sys(prtos_word_t val) {
    if (val == -1)
        flush_tlb();
    else
        flush_tlb_entry(val);

    return PRTOS_OK;
}
