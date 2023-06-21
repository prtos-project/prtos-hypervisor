/*
 * FILE: physmm.c
 *
 * Physical memory manager
 *
 * www.prtos.org
 */

#include <physmm.h>
#include <arch/physmm.h>

prtos_u8_t read_by_pass_mmu_byte(void *p_addr) {
    struct phys_page *page;
    prtos_u8_t *v_addr, val;

    if ((prtos_address_t)p_addr > CONFIG_PRTOS_OFFSET) return *(prtos_u8_t *)p_addr;
    page = phis_mm_find_anonymous_page((prtos_address_t)p_addr);
    v_addr = vcache_map_page((prtos_address_t)p_addr, page);
    val = *v_addr;
    vcache_unlock_page(page);

    return val;
}

prtos_u32_t read_by_pass_mmu_word(void *p_addr) {
    struct phys_page *page;
    prtos_u32_t *v_addr, val;

    if ((prtos_address_t)p_addr > CONFIG_PRTOS_OFFSET) return *(prtos_u32_t *)p_addr;
    page = phis_mm_find_anonymous_page((prtos_address_t)p_addr);
    v_addr = vcache_map_page((prtos_address_t)p_addr, page);
    val = *v_addr;
    vcache_unlock_page(page);

    return val;
}

void write_by_pass_mmu_word(void *p_addr, prtos_u32_t val) {
    struct phys_page *page;
    prtos_u32_t *v_addr;

    if ((prtos_address_t)p_addr > CONFIG_PRTOS_OFFSET) {
        *(prtos_u32_t *)p_addr = val;
    } else {
        page = phis_mm_find_anonymous_page((prtos_address_t)p_addr);
        v_addr = vcache_map_page((prtos_address_t)p_addr, page);
        *v_addr = val;
        vcache_unlock_page(page);
    }
}

prtos_address_t enable_by_pass_mmu(prtos_address_t addr, partition_t *p, struct phys_page **page) {
    *page = pmm_find_page(addr, p, 0);
    addr = (prtos_address_t)vcache_map_page(addr, *page);

    return addr;
}

inline void disable_by_pass_mmu(struct phys_page *page) {
    vcache_unlock_page(page);
}
