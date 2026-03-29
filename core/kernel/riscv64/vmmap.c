/*
 * FILE: vmmap.c
 *
 * RISC-V 64-bit virtual memory map management (Sv39)
 *
 * The hypervisor uses Sv39 3-level page tables for HS-mode.
 * Level 1 (VPN[2]): 512 entries, 1GB gigapages
 * Level 2 (VPN[1]): 512 entries, 2MB megapages
 * Level 3 (VPN[0]): 512 entries, 4KB pages
 *
 * The boot code (head.S) creates 1GB gigapage mappings.
 * setup_vm_map() pre-installs L2 tables (for L1) and L3 tables (for L2)
 * in the frame area so that vm_map_page() can directly write entries
 * without dynamic allocation (following the x86 pattern).
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
#include <arch/paging.h>

/* PTE flags */
#define PTE_V    (1UL << 0)
#define PTE_R    (1UL << 1)
#define PTE_W    (1UL << 2)
#define PTE_X    (1UL << 3)
#define PTE_G    (1UL << 5)
#define PTE_A    (1UL << 6)
#define PTE_D    (1UL << 7)

/* Leaf PTE: V + R/W/X + G + A + D */
#define PTE_RWXG (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define PTE_TABLE (PTE_V)  /* Non-leaf: V only, R=W=X=0 */

#define PTE_PPN_SHIFT 10

/* The boot root page table (from head.S, in .boot.data at physical address) */
/* Access via linker-provided virtual alias _page_tables (= _boot_pt_l1 - PHYSOFFSET) */
extern prtos_u64_t _page_tables[];

/* Runtime page tables: the boot L1 is also used as the runtime root.
 * We access it via its virtual address. */
static prtos_u64_t *hyp_pt_l1;  /* virtual address of root (L1) table */

/* Maximum number of L3 tables: 256MB / 2MB = 128 */
#define VMM_L3_POOL_COUNT (256 * 1024 * 1024 / LPAGE_SIZE)

void __VBOOT setup_vm_map(prtos_address_t *start_frame_area, prtos_s32_t *num_of_frames) {
    prtos_address_t st, end;
    prtos_s32_t num_l2, e, i;
    prtos_u8_t *rsv_l2;
    prtos_u8_t *rsv_l3;

    /* Get hypervisor physical memory area from config */
    st = prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].start_addr;
    end = st + prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].size - 1;

    /* Virtual address of the boot root page table (linker alias) */
    hyp_pt_l1 = (prtos_u64_t *)_page_tables;

    /* Frame area starts at the next 1GB boundary after the hypervisor gigapage.
     * This avoids splitting the hypervisor's gigapage (which also maps partition
     * memory and config data).  The frame area VAs are used exclusively by
     * vcache_map_page, so they don't need a 1:1 physical mapping. */
    *start_frame_area = (CONFIG_PRTOS_OFFSET & ~((prtos_address_t)(1ULL << PTDL1_SHIFT) - 1)) + (1ULL << PTDL1_SHIFT);
    *num_of_frames = ((PRTOS_VMAPEND - *start_frame_area) + 1) / PAGE_SIZE;

    /* Limit to a reasonable size */
    if (*num_of_frames > (256 * 1024 * 1024 / PAGE_SIZE))
        *num_of_frames = 256 * 1024 * 1024 / PAGE_SIZE;

    /* Allocate L2 tables for frame area via GET_MEMAZ (following x86 pattern) */
    num_l2 = (((PRTOS_VMAPEND - *start_frame_area) + 1) >> PTDL1_SHIFT);
    GET_MEMAZ(rsv_l2, PTDL2SIZE * num_l2, PTDL2SIZE);

    /* Install L2 tables in L1 for the frame area.
     * These L1 entries have no existing mapping, so no gigapage splitting needed. */
    for (e = VADDR_TO_VPN2(*start_frame_area); (e < PTDL1ENTRIES) && (num_l2 > 0); e++) {
        hyp_pt_l1[e] = ((_VIRT2PHYS((prtos_u64_t)(prtos_address_t)rsv_l2) >> 12) << PTE_PPN_SHIFT) | PTE_TABLE;
        rsv_l2 += PTDL2SIZE;
        num_l2--;
    }

    flush_tlb();

    /* Allocate L3 tables for the frame area (covers first 256MB) */
    GET_MEMAZ(rsv_l3, VMM_L3_POOL_COUNT * PTDL3SIZE, PTDL3SIZE);

    /* Pre-install L3 tables in the first L2 of the frame area (vpn1 starts at 0) */
    {
        prtos_u64_t *first_l2 = (prtos_u64_t *)_PHYS2VIRT(
            (hyp_pt_l1[VADDR_TO_VPN2(*start_frame_area)] >> PTE_PPN_SHIFT) << 12);
        prtos_s32_t num_l3 = VMM_L3_POOL_COUNT;

        for (i = 0; i < num_l3 && i < PTDL2ENTRIES; i++) {
            prtos_u8_t *l3 = rsv_l3 + i * PTDL3SIZE;
            first_l2[i] = ((_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l3) >> 12) << PTE_PPN_SHIFT) | PTE_TABLE;
        }
    }

    flush_tlb();
}

void setup_ptd_level_1_table(prtos_word_t *ptd_level_1, kthread_t *k) {
    /* Partition isolation uses G-stage (hgatp), not S-stage page tables */
}

prtos_u32_t vm_arch_attr_to_attr(prtos_u32_t entry) {
    prtos_u32_t flags = entry & (PAGE_SIZE - 1), attr = 0;
    if (flags & PTE_V) attr |= _PG_ATTR_PRESENT;
    if (flags & _PG_ARCH_WRITE) attr |= _PG_ATTR_RW;
    return attr | (flags & ~(_PG_ARCH_PRESENT | _PG_ARCH_USER | _PG_ARCH_RW));
}

prtos_u32_t vm_attr_to_arch_attr(prtos_u32_t flags) {
    prtos_u32_t attr = PTE_V | PTE_G | PTE_A | PTE_D;
    if (flags & _PG_ATTR_PRESENT) attr |= PTE_V;
    if (flags & _PG_ATTR_RW) attr |= PTE_R | PTE_W;
    else attr |= PTE_R;  /* At least readable */
    return attr;
}

prtos_s32_t vm_map_user_page(partition_t *k, prtos_word_t *ptd_level_1, prtos_address_t p_addr, prtos_address_t v_addr, prtos_u32_t flags,
                             prtos_address_t (*alloc)(struct prtos_conf_part *, prtos_u_size_t, prtos_u32_t, prtos_address_t *, prtos_s_size_t *),
                             prtos_address_t *pool, prtos_s_size_t *pool_size) {
    /* Partition mapping uses G-stage page tables, not S-stage */
    return 0;
}

void vm_map_page(prtos_address_t p_addr, prtos_address_t v_addr, prtos_word_t flags) {
    prtos_s32_t vpn2, vpn1, vpn0;
    prtos_u64_t *l2, *l3;

    ASSERT(!(p_addr & (PAGE_SIZE - 1)));
    ASSERT(!(v_addr & (PAGE_SIZE - 1)));
    ASSERT(v_addr >= CONFIG_PRTOS_OFFSET);

    vpn2 = VADDR_TO_VPN2(v_addr);
    vpn1 = VADDR_TO_VPN1(v_addr);
    vpn0 = VADDR_TO_VPN0(v_addr);

    /* L1 entry must be present (installed by setup_vm_map or boot gigapage) */
    ASSERT(hyp_pt_l1[vpn2] & PTE_V);
    /* Must be a table entry (non-leaf), not a gigapage */
    ASSERT(!(hyp_pt_l1[vpn2] & (PTE_R | PTE_W | PTE_X)));

    l2 = (prtos_u64_t *)_PHYS2VIRT(((hyp_pt_l1[vpn2] >> PTE_PPN_SHIFT) << 12));

    /* L2 entry must be present (L3 pre-installed by setup_vm_map) */
    ASSERT(l2[vpn1] & PTE_V);
    /* Must be a table entry (non-leaf), not a megapage */
    ASSERT(!(l2[vpn1] & (PTE_R | PTE_W | PTE_X)));

    l3 = (prtos_u64_t *)_PHYS2VIRT(((l2[vpn1] >> PTE_PPN_SHIFT) << 12));

    /* Install 4KB page mapping */
    l3[vpn0] = ((p_addr >> 12) << PTE_PPN_SHIFT) | vm_attr_to_arch_attr(flags);
    flush_tlb_entry(v_addr);
}

void flush_tlb(void) {
    __asm__ __volatile__("sfence.vma" ::: "memory");
}

void flush_tlb_entry(prtos_address_t addr) {
    __asm__ __volatile__("sfence.vma %0" : : "r"(addr) : "memory");
}

prtos_address_t SET_PTE_UNCACHED(prtos_address_t val) {
    return val;
}

/* Convert IPA (physical) to virtual address using the VA/PA offset.
 * Mask to valid physical address range to clear sign-extension from
 * 32-bit guest address computations (lw sign-extends to 64 bits). */
void *prtos_ipa_to_va(prtos_u64_t ipa) {
    ipa &= 0xFFFFFFFFULL;
    return (void *)_PHYS2VIRT(ipa);
}
