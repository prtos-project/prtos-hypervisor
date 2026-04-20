/*
 * FILE: paging.h
 *
 * LoongArch 64-bit paging definitions
 *
 * LoongArch uses TLB-based virtual memory and Direct Mapped Windows (DMW).
 * The hypervisor uses DMW for direct PA→VA mapping, avoiding page tables
 * for kernel space. Guest isolation uses LVZ guest TLBs.
 *
 * http://www.prtos.org/
 */

#ifndef _PRTOS_ARCH_PAGING_H_
#define _PRTOS_ARCH_PAGING_H_

#define PAGE_SHIFT 12
#define PAGE_SIZE 4096
#define PRTOS_VMAPSTART CONFIG_PRTOS_OFFSET
#define PRTOS_VMAPSIZE ((PRTOS_VMAPEND - PRTOS_VMAPSTART) + 1)

#define LPAGE_SIZE (2 * 1024 * 1024)  /* 2MB large page */
#define PRTOS_VMAPEND 0x9FFFFFFFFFFFFFFFULL  /* End of DMW cached region */

#define PTD_LEVELS 3  /* 3-level page tables */

#define PTDL1_SHIFT 30
#define PTDL2_SHIFT 21
#define PTDL3_SHIFT 12

#define PTDL1SIZE 4096
#define PTDL2SIZE 4096
#define PTDL3SIZE 4096

#define PTDL1ENTRIES 512
#define PTDL2ENTRIES 512
#define PTDL3ENTRIES 512

#define PRTOS_PCTRLTAB_ADDR (CONFIG_PRTOS_OFFSET - 256 * 1024)

/* Virtual address index macros (for page-table walking when needed) */
#define VADDR_TO_VPN2(va) (((prtos_u64_t)(va) >> 30) & 0x1FF)
#define VADDR_TO_VPN1(va) (((prtos_u64_t)(va) >> 21) & 0x1FF)
#define VADDR_TO_VPN0(va) (((prtos_u64_t)(va) >> 12) & 0x1FF)

#ifdef _PRTOS_KERNEL_
#ifndef __ASSEMBLY__
/*
 * LoongArch DMW mapping:
 *  VA = PA | 0x9000000000000000 (cached)
 *  PA = VA & ~0xF000000000000000
 *
 * _VIRT2PHYS/PHYS2VIRT use CONFIG_PRTOS_OFFSET and CONFIG_PRTOS_LOAD_ADDR
 * for compatibility with the common PRTOS code, but the actual transform
 * is just adding/removing the DMW prefix.
 */
#define _VIRT2PHYS(x) ((prtos_u64_t)(x) - (prtos_u64_t)CONFIG_PRTOS_OFFSET + (prtos_u64_t)CONFIG_PRTOS_LOAD_ADDR)
#define _PHYS2VIRT(x) ((prtos_u64_t)(x) + (prtos_u64_t)CONFIG_PRTOS_OFFSET - (prtos_u64_t)CONFIG_PRTOS_LOAD_ADDR)

void flush_tlb(void);
void flush_tlb_entry(prtos_address_t addr);
#else
#define _VIRT2PHYS(x) ((x) - CONFIG_PRTOS_OFFSET + CONFIG_PRTOS_LOAD_ADDR)
#define _PHYS2VIRT(x) ((x) + CONFIG_PRTOS_OFFSET - CONFIG_PRTOS_LOAD_ADDR)
#endif

#define PAGE_MASK (~(PAGE_SIZE - 1))
#define LPAGE_MASK (~(LPAGE_SIZE - 1))

/* LoongArch TLB entry format flags */
#define _PG_ARCH_VALID    0x001  /* V (valid) */
#define _PG_ARCH_DIRTY    0x002  /* D (dirty/writable) */
#define _PG_ARCH_PLV_MASK 0x00C  /* PLV (privilege level, 2 bits) */
#define _PG_ARCH_MAT_MASK 0x070  /* MAT (memory access type, 3 bits) */
#define _PG_ARCH_GLOBAL   0x200  /* G (global) */

/* Compatibility defines for shared code */
#define _PG_ARCH_PRESENT  _PG_ARCH_VALID
#define _PG_ARCH_WRITE    _PG_ARCH_DIRTY
#define _PG_ARCH_RW       _PG_ARCH_DIRTY
#define _PG_ARCH_USER     0x008  /* PLV3 access */
#define _PG_ARCH_ADDR     (~0xFFFULL)

#define IS_PTD_PRESENT(x) ((x) & _PG_ARCH_VALID)
#define IS_PTE_PRESENT(x) ((x) & _PG_ARCH_VALID)
#define SET_PTD_NOT_PRESENT(x) ((x) & ~_PG_ARCH_VALID)
#define SET_PTE_NOT_PRESENT(x) ((x) & ~_PG_ARCH_VALID)
#define SET_PTE_RONLY(x) ((x) & ~_PG_ARCH_DIRTY)
#define SET_PTE_UNCACHED(x) (x)  /* Handled via DMW for uncached access */
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
