/*
 * FILE: paging.h
 *
 * i386 paging
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_PAGING_H_
#define _PRTOS_ARCH_PAGING_H_

#define PAGE_SHIFT 12
#define PAGE_SIZE 4096
#define PRTOS_VMAPSTART CONFIG_PRTOS_OFFSET
#define PRTOS_VMAPSIZE ((PRTOS_VMAPEND - PRTOS_VMAPSTART) + 1)

#define LPAGE_SIZE (4 * 1024 * 1024)
#define PRTOS_VMAPEND 0xffffffffUL

#define PTD_LEVELS 2

#define PTDL1_SHIFT 22
#define PTDL2_SHIFT 12

#define PTDL1SIZE 4096
#define PTDL2SIZE 4096

#define PTDL1ENTRIES 1024
#define PTDL2ENTRIES 1024

#define VADDR_TO_PDE_INDEX(x) (((x)&0xffc00000UL) >> PTDL1_SHIFT)
#define VADDR_TO_PTE_INDEX(x) (((x)&0x3ff000UL) >> PTDL2_SHIFT)

#define PDE_AND_PTE_TO_VADDR(l1, l2, l3) (((l1) << PTDL1_SHIFT) | ((l2) << PTDL2_SHIFT))
#define PRTOS_PCTRLTAB_ADDR (CONFIG_PRTOS_OFFSET - 256 * 1024)

#ifdef _PRTOS_KERNEL_
#ifndef __ASSEMBLY__
#define _VIRT2PHYS(x) ((prtos_u32_t)(x)-CONFIG_PRTOS_OFFSET + CONFIG_PRTOS_LOAD_ADDR)
#define _PHYS2VIRT(x) ((prtos_u32_t)(x) + CONFIG_PRTOS_OFFSET - CONFIG_PRTOS_LOAD_ADDR)

extern prtos_address_t _page_tables[];
#else
#define _VIRT2PHYS(x) ((x)-CONFIG_PRTOS_OFFSET + CONFIG_PRTOS_LOAD_ADDR)
#define _PHYS2VIRT(x) ((x) + CONFIG_PRTOS_OFFSET - CONFIG_PRTOS_LOAD_ADDR)
#endif

#define PAGE_MASK (~(PAGE_SIZE - 1))
#define LPAGE_MASK (~(LPAGE_SIZE - 1))

/* Page directory/table options */

#define _PG_ARCH_PRESENT 0x001
#define _PG_ARCH_RW 0x002
#define _PG_ARCH_USER 0x004
#define _PG_ARCH_PWT 0x008
#define _PG_ARCH_PCD 0x010
#define _PG_ARCH_ACCESSED 0x020
#define _PG_ARCH_DIRTY 0x040
#define _PG_ARCH_PSE 0x080
#define _PG_ARCH_GLOBAL 0x100
#define _PG_ARCH_UNUSED1 0x200
#define _PG_ARCH_UNUSED2 0x400
#define _PG_ARCH_UNUSED3 0x800
#define _PG_ARCH_PS 0x80
#define _PG_ARCH_PAT 0x100
#define _PG_ARCH_ADDR (~0xfff)

#define IS_PTD_PRESENT(x) ((x)&_PG_ARCH_PRESENT)
#define IS_PTE_PRESENT(x) ((x)&_PG_ARCH_PRESENT)
#define SET_PTD_NOT_PRESENT(x) ((x) & ~_PG_ARCH_PRESENT)
#define SET_PTE_NOT_PRESENT(x) ((x) & ~_PG_ARCH_PRESENT)
#define SET_PTE_RONLY(x) ((x) & ~_PG_ARCH_RW)
#define SET_PTE_UNCACHED(x) ((x) & ~_PG_ARCH_PCD)
#define GET_PTD_ADDR(x) ((x)&PAGE_MASK)
#define GET_PTE_ADDR(x) ((x)&PAGE_MASK)
#define GET_USER_PTD_ENTRIES(type) VADDR_TO_PDE_INDEX(CONFIG_PRTOS_OFFSET)

#define GET_USER_PTE_ENTRIES(type) PTDL2ENTRIES
#define CLONE_PRTOS_PTD_ENTRIES(type, vPtd) \
    if ((type) == PPAG_PTDL1) setup_ptd_level_1_table(vPtd, GET_LOCAL_PROCESSOR()->sched.current_kthread)
#define IS_VALID_PTD_PTR(type, p_addr) (((type) == PPAG_PTDL1) && (((p_addr) >> 2) & (PTDL1ENTRIES - 1)) < VADDR_TO_PDE_INDEX(CONFIG_PRTOS_OFFSET))

#define IS_VALID_PTD_ENTRY(type) ((type) == PPAG_PTDL2)
#define IS_VALID_PTE_ENTRY(type) (1)

#endif
#endif
