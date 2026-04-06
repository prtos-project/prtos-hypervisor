/*
 * FILE: paging.h
 *
 * AArch64 paging (4KB granule, 4-level page tables, 48-bit VA)
 *
 * VA layout (L0 index partitioning):
 *   L0[0..19]    10 TB   Partition / IO / shared memory
 *   L0[20]      512 GB   PRTOS kernel virtual space
 *   L0[21..255] reserved
 *   L0[256..265] 5 TB    Directmap (PA linear mapping)
 *   L0[266..511] reserved
 *
 * http://www.prtos.org/
 */

#ifndef _PRTOS_ARCH_PAGING_H_
#define _PRTOS_ARCH_PAGING_H_

#define PAGE_SHIFT 12
#define PAGE_SIZE 4096

#define LPAGE_SIZE (2 * 1024 * 1024)  /* 2MB block descriptor */

#define PTD_LEVELS 4  /* 4 levels for 48-bit VA */

/* Page table level shifts: L0[47:39], L1[38:30], L2[29:21], L3[20:12] */
#define PTDL0_SHIFT 39
#define PTDL1_SHIFT 30
#define PTDL2_SHIFT 21
#define PTDL3_SHIFT 12

#define PTDL0SIZE 4096
#define PTDL1SIZE 4096
#define PTDL2SIZE 4096
#define PTDL3SIZE 4096

#define PTDL0ENTRIES 512
#define PTDL1ENTRIES 512
#define PTDL2ENTRIES 512
#define PTDL3ENTRIES 512

/* L0 index constants for the VA layout */
#define L0_IDX_KERNEL       20
#define L0_IDX_DIRECTMAP    256

/* Key virtual address bases */
#define KERNEL_VA_BASE      (((prtos_u64_t)L0_IDX_KERNEL) << PTDL0_SHIFT)       /* 0xA0000000000 */
#define DIRECTMAP_VA_BASE   (((prtos_u64_t)L0_IDX_DIRECTMAP) << PTDL0_SHIFT)    /* 0x800000000000 */

/* Virtual map range: kernel occupies L0[20] (512GB slot) */
#define PRTOS_VMAPSTART CONFIG_PRTOS_OFFSET
#define PRTOS_VMAPEND   (KERNEL_VA_BASE + (512ULL << 30) - 1)  /* end of L0[20] */
#define PRTOS_VMAPSIZE  ((PRTOS_VMAPEND - PRTOS_VMAPSTART) + 1)

#define PRTOS_PCTRLTAB_ADDR (CONFIG_PRTOS_OFFSET - 256 * 1024)

/* Page table index macros: VA[47:39]=L0, VA[38:30]=L1, VA[29:21]=L2, VA[20:12]=L3 */
#define VADDR_TO_VPN3(va) (((prtos_u64_t)(va) >> 39) & 0x1FF)  /* L0 index */
#define VADDR_TO_VPN2(va) (((prtos_u64_t)(va) >> 30) & 0x1FF)  /* L1 index */
#define VADDR_TO_VPN1(va) (((prtos_u64_t)(va) >> 21) & 0x1FF)  /* L2 index */
#define VADDR_TO_VPN0(va) (((prtos_u64_t)(va) >> 12) & 0x1FF)  /* L3 index */

#ifdef _PRTOS_KERNEL_
#ifndef __ASSEMBLY__
/* Kernel linear mapping: VA = PA + (PRTOS_OFFSET - PRTOS_LOAD_ADDR) */
#define _VIRT2PHYS(x) ((prtos_u64_t)(x) - (prtos_u64_t)CONFIG_PRTOS_OFFSET + (prtos_u64_t)CONFIG_PRTOS_LOAD_ADDR)
#define _PHYS2VIRT(x) ((prtos_u64_t)(x) + (prtos_u64_t)CONFIG_PRTOS_OFFSET - (prtos_u64_t)CONFIG_PRTOS_LOAD_ADDR)

extern prtos_address_t _page_tables[];

void flush_tlb(void);
void flush_tlb_entry(prtos_address_t addr);
#else
#define _VIRT2PHYS(x) ((x) - CONFIG_PRTOS_OFFSET + CONFIG_PRTOS_LOAD_ADDR)
#define _PHYS2VIRT(x) ((x) + CONFIG_PRTOS_OFFSET - CONFIG_PRTOS_LOAD_ADDR)
#endif

#define PAGE_MASK (~(PAGE_SIZE - 1))
#define LPAGE_MASK (~(LPAGE_SIZE - 1))

/* AArch64 page table descriptor bits */
#define _PG_ARCH_VALID    0x001  /* Bit[0]: valid */
#define _PG_ARCH_TABLE    0x002  /* Bit[1]: table (vs block) at L0/L1/L2 */
#define _PG_ARCH_AF       (1UL << 10) /* Access flag */
#define _PG_ARCH_SH_INNER (3UL << 8)  /* Inner Shareable */
#define _PG_ARCH_AP_RW    (0UL << 7)  /* AP[2:1] = 00: EL2 RW */
#define _PG_ARCH_AP_RO    (1UL << 7)  /* AP[2:1] = 10: EL2 RO */
#define _PG_ARCH_MAIR_IDX0 (0UL << 2) /* AttrIndx[2:0] = 0 (normal memory) */

/* Compatibility defines for shared code */
#define _PG_ARCH_PRESENT  _PG_ARCH_VALID
#define _PG_ARCH_USER     0  /* No user bit on AArch64 EL2 */
#define _PG_ARCH_RW       (1UL << 7)
#define _PG_ARCH_WRITE    _PG_ARCH_RW
#define _PG_ARCH_ADDR     (~0xFFFULL)

#define IS_PTD_PRESENT(x) ((x) & _PG_ARCH_VALID)
#define IS_PTE_PRESENT(x) ((x) & _PG_ARCH_VALID)
#define SET_PTD_NOT_PRESENT(x) ((x) & ~_PG_ARCH_VALID)
#define SET_PTE_NOT_PRESENT(x) ((x) & ~_PG_ARCH_VALID)
#define SET_PTE_RONLY(x) ((x) | (1UL << 7))
#define SET_PTE_UNCACHED(x) (x)
#define GET_PTD_ADDR(x) ((x) & PAGE_MASK)
#define GET_PTE_ADDR(x) ((x) & PAGE_MASK)
#define GET_USER_PTD_ENTRIES(type) PTDL1ENTRIES
#define GET_USER_PTE_ENTRIES(type) PTDL3ENTRIES

#define CLONE_PRTOS_PTD_ENTRIES(type, vPtd) \
    if ((type) == PPAG_PTDL1) setup_ptd_level_1_table(vPtd, GET_LOCAL_PROCESSOR()->sched.current_kthread)
#define IS_VALID_PTD_PTR(type, p_addr) (1)
#define IS_VALID_PTD_ENTRY(type) ((type) == PPAG_PTDL2)
#define IS_VALID_PTE_ENTRY(type) (1)

#endif
#endif
