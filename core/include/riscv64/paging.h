/*
 * FILE: paging.h
 *
 * RISC-V 64-bit paging (Sv39)
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_PAGING_H_
#define _PRTOS_ARCH_PAGING_H_

#define PAGE_SHIFT 12
#define PAGE_SIZE 4096
#define PRTOS_VMAPSTART CONFIG_PRTOS_OFFSET
#define PRTOS_VMAPSIZE ((PRTOS_VMAPEND - PRTOS_VMAPSTART) + 1)

#define LPAGE_SIZE (2 * 1024 * 1024)  /* 2MB mega page for Sv39 */
#define PRTOS_VMAPEND 0xFFFFFFFFFFFFFFFFUL  /* Sv39 upper canonical end */

#define PTD_LEVELS 3  /* Sv39: 3 levels */

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

/* Sv39 page table index macros */
#define VADDR_TO_VPN2(va) (((prtos_u64_t)(va) >> 30) & 0x1FF)
#define VADDR_TO_VPN1(va) (((prtos_u64_t)(va) >> 21) & 0x1FF)
#define VADDR_TO_VPN0(va) (((prtos_u64_t)(va) >> 12) & 0x1FF)

/* Satp CSR: MODE=Sv39 (8), ASID=0, PPN=root>>12 */
#define SATP_MODE_SV39 (8UL << 60)

#ifdef _PRTOS_KERNEL_
#ifndef __ASSEMBLY__
#define _VIRT2PHYS(x) ((prtos_u64_t)(x) - (prtos_u64_t)CONFIG_PRTOS_OFFSET + (prtos_u64_t)CONFIG_PRTOS_LOAD_ADDR)
#define _PHYS2VIRT(x) ((prtos_u64_t)(x) + (prtos_u64_t)CONFIG_PRTOS_OFFSET - (prtos_u64_t)CONFIG_PRTOS_LOAD_ADDR)

extern prtos_address_t _page_tables[];
#else
#define _VIRT2PHYS(x) ((x) - CONFIG_PRTOS_OFFSET + CONFIG_PRTOS_LOAD_ADDR)
#define _PHYS2VIRT(x) ((x) + CONFIG_PRTOS_OFFSET - CONFIG_PRTOS_LOAD_ADDR)
#endif

#define PAGE_MASK (~(PAGE_SIZE - 1))
#define LPAGE_MASK (~(LPAGE_SIZE - 1))

/* RISC-V Sv39 page table entry bits */
#define _PG_ARCH_VALID    0x001  /* V */
#define _PG_ARCH_READ     0x002  /* R */
#define _PG_ARCH_WRITE    0x004  /* W */
#define _PG_ARCH_EXEC     0x008  /* X */
#define _PG_ARCH_USER     0x010  /* U */
#define _PG_ARCH_GLOBAL   0x020  /* G */
#define _PG_ARCH_ACCESSED 0x040  /* A */
#define _PG_ARCH_DIRTY    0x080  /* D */

/* Compatibility defines for shared code */
#define _PG_ARCH_PRESENT  _PG_ARCH_VALID
#define _PG_ARCH_RW       _PG_ARCH_WRITE
#define _PG_ARCH_ADDR     (~0xFFFULL)

#define IS_PTD_PRESENT(x) ((x) & _PG_ARCH_VALID)
#define IS_PTE_PRESENT(x) ((x) & _PG_ARCH_VALID)
#define SET_PTD_NOT_PRESENT(x) ((x) & ~_PG_ARCH_VALID)
#define SET_PTE_NOT_PRESENT(x) ((x) & ~_PG_ARCH_VALID)
#define SET_PTE_RONLY(x) ((x) & ~_PG_ARCH_WRITE)
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
