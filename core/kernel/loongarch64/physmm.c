/*
 * FILE: physmm.c
 *
 * LoongArch 64-bit physical memory access (bypass MMU)
 *
 * With DMW, physical memory is always directly accessible via
 * the uncached DMW window (0x8000000000000000 | PA).
 *
 * http://www.prtos.org/
 */

#include <physmm.h>
#include <arch/physmm.h>

prtos_u8_t read_by_pass_mmu_byte(void *p_addr) {
    return *(prtos_u8_t *)p_addr;
}

prtos_u32_t read_by_pass_mmu_word(void *p_addr) {
    return *(prtos_u32_t *)p_addr;
}

void write_by_pass_mmu_word(void *p_addr, prtos_u32_t val) {
    *(prtos_u32_t *)p_addr = val;
}

prtos_address_t enable_by_pass_mmu(prtos_address_t addr, partition_t *p, struct phys_page **page) {
    return addr;
}

inline void disable_by_pass_mmu(struct phys_page *page) {
}
