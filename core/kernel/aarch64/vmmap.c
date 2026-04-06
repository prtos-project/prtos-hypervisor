/*
 * FILE: vmmap.c
 *
 * AArch64 virtual memory map management for EL2
 *
 * The hypervisor uses 4-level page tables (TTBR0_EL2, 4KB granule, 48-bit VA).
 * Level 0: 512 entries, 512GB per entry  (VA[47:39])
 * Level 1: 512 entries, 1GB blocks       (VA[38:30])
 * Level 2: 512 entries, 2MB blocks       (VA[29:21])
 * Level 3: 512 entries, 4KB pages        (VA[20:12])
 *
 * The boot code creates 1GB block mappings at L1. setup_vm_map() installs
 * L2 and L3 tables in the kernel's L1 table (within L0[20]) for the
 * frame area used by vm_map_page().
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

/* Forward declarations */
void flush_tlb(void);
void flush_tlb_entry(prtos_address_t addr);

/* Descriptor bits */
#define DESC_VALID    (1UL << 0)
#define DESC_TABLE    (1UL << 1)
#define DESC_BLOCK    (0UL << 1)
#define DESC_PAGE     (1UL << 1)
/* Attributes for normal memory */
#define ATTR_AF       (1UL << 10)
#define ATTR_ISH      (3UL << 8)
#define ATTR_IDX0     (0UL << 2)  /* AttrIndx=0 (Normal WB from MAIR) */
#define ATTR_AP_RW    (0UL << 7)  /* EL2 R/W */

#define LEAF_ATTRS (DESC_VALID | DESC_PAGE | ATTR_IDX0 | ATTR_AF | ATTR_ISH | ATTR_AP_RW)
#define TABLE_DESC (DESC_VALID | DESC_TABLE)

#define PTE_PPN_MASK (~0xFFFULL & ((1ULL << 48) - 1))

extern prtos_u64_t _page_tables[];       /* L0 root (virtual address) */
extern prtos_u64_t _page_tables_l1_kern[];  /* L1 kernel table (virtual address) */

static prtos_u64_t *hyp_pt_l0;   /* L0 root table */
static prtos_u64_t *hyp_pt_l1;   /* L1 table for kernel region (L0[20]) */

#define VMM_L3_POOL_COUNT (256 * 1024 * 1024 / LPAGE_SIZE)

void __VBOOT setup_vm_map(prtos_address_t *start_frame_area, prtos_s32_t *num_of_frames) {
    prtos_address_t st, end;
    prtos_s32_t num_l2, e, i;
    prtos_u8_t *rsv_l2;
    prtos_u8_t *rsv_l3;

    st = prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].start_addr;
    end = st + prtos_conf_phys_mem_area_table[prtos_conf_table.hpv.physical_memory_areas_offset].size - 1;

    hyp_pt_l0 = (prtos_u64_t *)_page_tables;
    /* L1 for kernel is L0[20]'s target; use the linker-provided virtual alias */
    hyp_pt_l1 = (prtos_u64_t *)_page_tables_l1_kern;

    /* Frame area starts at the next 1GB boundary after the kernel's L1[0] gigapage */
    *start_frame_area = (CONFIG_PRTOS_OFFSET & ~((prtos_address_t)(1ULL << PTDL1_SHIFT) - 1)) + (1ULL << PTDL1_SHIFT);
    *num_of_frames = ((PRTOS_VMAPEND - *start_frame_area) + 1) / PAGE_SIZE;

    if (*num_of_frames > (256 * 1024 * 1024 / PAGE_SIZE))
        *num_of_frames = 256 * 1024 * 1024 / PAGE_SIZE;

    /* Number of L2 tables needed (one per 1GB slot in frame area within this L1) */
    num_l2 = (((PRTOS_VMAPEND - *start_frame_area) + 1) >> PTDL1_SHIFT);
    GET_MEMAZ(rsv_l2, PTDL2SIZE * num_l2, PTDL2SIZE);

    /* Install L2 tables in the kernel L1 for the frame area */
    for (e = VADDR_TO_VPN2(*start_frame_area); (e < PTDL1ENTRIES) && (num_l2 > 0); e++) {
        hyp_pt_l1[e] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)rsv_l2) & PTE_PPN_MASK) | TABLE_DESC;
        rsv_l2 += PTDL2SIZE;
        num_l2--;
    }

    flush_tlb();

    /* Pre-allocate L3 tables for the first 256MB of frame area */
    GET_MEMAZ(rsv_l3, VMM_L3_POOL_COUNT * PTDL3SIZE, PTDL3SIZE);

    {
        prtos_u64_t *first_l2 = (prtos_u64_t *)_PHYS2VIRT(
            hyp_pt_l1[VADDR_TO_VPN2(*start_frame_area)] & PTE_PPN_MASK);
        prtos_s32_t num_l3 = VMM_L3_POOL_COUNT;

        for (i = 0; i < num_l3 && i < PTDL2ENTRIES; i++) {
            prtos_u8_t *l3 = rsv_l3 + i * PTDL3SIZE;
            first_l2[i] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l3) & PTE_PPN_MASK) | TABLE_DESC;
        }
    }

    flush_tlb();
}

void setup_ptd_level_1_table(prtos_word_t *ptd_level_1, kthread_t *k) {
}

prtos_u32_t vm_arch_attr_to_attr(prtos_u32_t entry) {
    prtos_u32_t flags = entry & (PAGE_SIZE - 1), attr = 0;
    if (flags & DESC_VALID) attr |= _PG_ATTR_PRESENT;
    if (!(flags & (1UL << 7))) attr |= _PG_ATTR_RW;  /* AP[2]=0 means RW */
    return attr;
}

prtos_u32_t vm_attr_to_arch_attr(prtos_u32_t flags) {
    prtos_u32_t attr = DESC_VALID | DESC_PAGE | ATTR_AF | ATTR_ISH | ATTR_IDX0;
    if (!(flags & _PG_ATTR_RW)) attr |= (1UL << 7);  /* AP[2]=1: RO */
    return attr;
}

prtos_s32_t vm_map_user_page(partition_t *k, prtos_word_t *ptd_level_1, prtos_address_t p_addr, prtos_address_t v_addr, prtos_u32_t flags,
                             prtos_address_t (*alloc)(struct prtos_conf_part *, prtos_u_size_t, prtos_u32_t, prtos_address_t *, prtos_s_size_t *),
                             prtos_address_t *pool, prtos_s_size_t *pool_size) {
    return 0;
}

void vm_map_page(prtos_address_t p_addr, prtos_address_t v_addr, prtos_word_t flags) {
    prtos_s32_t vpn2, vpn1, vpn0;
    prtos_u64_t *l2, *l3;

    ASSERT(!(p_addr & (PAGE_SIZE - 1)));
    ASSERT(!(v_addr & (PAGE_SIZE - 1)));
    ASSERT(v_addr >= CONFIG_PRTOS_OFFSET);

    /* For addresses in the kernel region (L0[20]), walk L1→L2→L3.
     * L0 and L1 are known at boot time; we only need the L1→L2→L3 walk. */
    vpn2 = VADDR_TO_VPN2(v_addr);  /* L1 index */
    vpn1 = VADDR_TO_VPN1(v_addr);  /* L2 index */
    vpn0 = VADDR_TO_VPN0(v_addr);  /* L3 index */

    ASSERT(hyp_pt_l1[vpn2] & DESC_VALID);
    ASSERT(hyp_pt_l1[vpn2] & DESC_TABLE);

    l2 = (prtos_u64_t *)_PHYS2VIRT(hyp_pt_l1[vpn2] & PTE_PPN_MASK);

    ASSERT(l2[vpn1] & DESC_VALID);
    ASSERT(l2[vpn1] & DESC_TABLE);

    l3 = (prtos_u64_t *)_PHYS2VIRT(l2[vpn1] & PTE_PPN_MASK);

    l3[vpn0] = (p_addr & PTE_PPN_MASK) | vm_attr_to_arch_attr(flags);
    flush_tlb_entry(v_addr);
}

void flush_tlb(void) {
    __asm__ __volatile__(
        "dsb ishst\n\t"
        "tlbi alle2\n\t"
        "dsb ish\n\t"
        "isb\n\t"
        ::: "memory"
    );
}

void flush_tlb_entry(prtos_address_t addr) {
    __asm__ __volatile__(
        "dsb ishst\n\t"
        "tlbi vae2, %0\n\t"
        "dsb ish\n\t"
        "isb\n\t"
        : : "r"(addr >> 12) : "memory"
    );
}

void *prtos_ipa_to_va(prtos_u64_t ipa) {
    /* On QEMU virt, partition IPAs start at 0 but actual RAM starts at 0x40000000.
     * The RSW loads partition data at PA = IPA + offset. The hypervisor must apply
     * the same offset to convert a guest IPA to a kernel VA. */
    prtos_u64_t pa = (ipa & 0xFFFFFFFFULL) + 0x40000000ULL;
    return (void *)_PHYS2VIRT(pa);
}
