/*
 * FILE: vmmap.c
 *
 * LoongArch 64-bit virtual memory map management
 *
 * The hypervisor uses DMW (Direct Mapped Windows) for kernel VA space.
 * No page tables are needed for kernel-space direct mapping.
 * vm_map_page() is used for dynamic frame area mappings.
 *
 * http://www.prtos.org/
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
#include <arch/paging.h>

void flush_tlb(void) {
    /* LoongArch: invtlb to flush all TLB entries */
    __asm__ __volatile__("invtlb 0x0, $zero, $zero" ::: "memory");
}

void flush_tlb_entry(prtos_address_t addr) {
    /* LoongArch: flush specific TLB entry (by VA) */
    __asm__ __volatile__("invtlb 0x5, $zero, %0" : : "r"(addr) : "memory");
}

void __VBOOT setup_vm_map(prtos_address_t *start_frame_area, prtos_s32_t *num_of_frames) {
    /*
     * LoongArch DMW gives us direct PA↔VA mapping for the entire physical
     * address space. No page table setup is needed for kernel direct access.
     *
     * For the frame area (used by vcache_map_page), we pick a VA range
     * in the DMW space and expose it as the frame area.
     */
    prtos_address_t st;
    st = prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].start_addr;

    /* Frame area starts at next 1GB boundary after the hypervisor region */
    *start_frame_area = (CONFIG_PRTOS_OFFSET & ~((prtos_address_t)(1ULL << PTDL1_SHIFT) - 1)) + (1ULL << PTDL1_SHIFT);
    *num_of_frames = ((PRTOS_VMAPEND - *start_frame_area) + 1) / PAGE_SIZE;

    /* Limit to 256MB */
    if (*num_of_frames > (256 * 1024 * 1024 / PAGE_SIZE))
        *num_of_frames = 256 * 1024 * 1024 / PAGE_SIZE;
}

void setup_ptd_level_1_table(prtos_word_t *ptd_level_1, kthread_t *k) {
    /* Partition isolation uses stage-2 (LVZ guest TLBs), not S-stage page tables */
}

prtos_u32_t vm_arch_attr_to_attr(prtos_u32_t entry) {
    prtos_u32_t flags = entry & (PAGE_SIZE - 1), attr = 0;
    if (flags & _PG_ARCH_PRESENT) attr |= _PG_ATTR_PRESENT;
    if (flags & _PG_ARCH_WRITE) attr |= _PG_ATTR_RW;
    return attr | (flags & ~(_PG_ARCH_PRESENT | _PG_ARCH_USER | _PG_ARCH_RW));
}

prtos_u32_t vm_attr_to_arch_attr(prtos_u32_t flags) {
    prtos_u32_t attr = _PG_ARCH_VALID | _PG_ARCH_GLOBAL;
    if (flags & _PG_ATTR_PRESENT) attr |= _PG_ARCH_VALID;
    if (flags & _PG_ATTR_RW) attr |= _PG_ARCH_DIRTY;
    return attr;
}

prtos_s32_t vm_map_user_page(partition_t *k, prtos_word_t *ptd_level_1, prtos_address_t p_addr, prtos_address_t v_addr, prtos_u32_t flags,
                             prtos_address_t (*alloc)(struct prtos_conf_part *, prtos_u_size_t, prtos_u32_t, prtos_address_t *, prtos_s_size_t *),
                             prtos_address_t *pool, prtos_s_size_t *pool_size) {
    /* Partition mapping uses guest TLBs via LVZ, not S-stage */
    return 0;
}

void vm_map_page(prtos_address_t p_addr, prtos_address_t v_addr, prtos_word_t flags) {
    /*
     * With DMW, any physical address is accessible via:
     *   cached:   PA | 0x9000000000000000
     *   uncached: PA | 0x8000000000000000
     *
     * vm_map_page is a no-op for DMW-based mapping since all physical
     * memory is already accessible. The vcache layer just computes the
     * DMW virtual address.
     */
}

/* Convert IPA (physical) to virtual address using DMW mapping */
void *prtos_ipa_to_va(prtos_u64_t ipa) {
    ipa &= 0xFFFFFFFFULL;
    return (void *)_PHYS2VIRT(ipa);
}
