/*
 * FILE: setup.c
 *
 * Setting up and starting up the kernel (arch dependent part)
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <arch/asm.h>
#include <arch/processor.h>
#include <arch/physmm.h>

prtos_u32_t __BOOT boot_detect_cpu_feature(void) {
    prtos_u32_t feat = 0, flags0, flags1, eax, ebx, ecx, edx;

    hw_save_flags(flags0);
    flags1 = flags0 | _CPU_FLAG_AC;
    hw_restore_flags(flags1);
    hw_save_flags(flags1);

    if (!(flags1 & _CPU_FLAG_AC)) return feat;

    flags1 = flags0 | _CPU_FLAG_ID;
    hw_restore_flags(flags1);
    hw_save_flags(flags1);
    if (!(flags1 & _CPU_FLAG_ID)) return feat;

    cpu_id(0, &eax, &ebx, &ecx, &edx);
    if (!eax) return feat;

    feat |= _DETECTED_I586;

    cpu_id(1, &eax, &ebx, &ecx, &edx);

    if (edx & _CPUID_PAE) feat |= _PAE_SUPPORT;

    if (edx & _CPUID_PSE) feat |= _PSE_SUPPORT;

    if (edx & _CPUID_PGE) feat |= _PGE_SUPPORT;

    if (ecx & _CPUID_X2APIC) feat |= _X2APIC_SUPPORT;

    return feat;
}

void __BOOT boot_init_page_table(void) {
    extern prtos_address_t _page_tables[];
    prtos_u32_t *ptd_level_1, *ptd_level_2;
    prtos_s32_t addr;

    ptd_level_1 = (prtos_u32_t *)_VIRT2PHYS(_page_tables);                // Page directory table
    ptd_level_2 = (prtos_u32_t *)(_VIRT2PHYS(_page_tables) + PAGE_SIZE);  // Page  table

    ptd_level_1[VADDR_TO_PDE_INDEX(0)] = (prtos_u32_t)ptd_level_2 | _PG_ARCH_PRESENT | _PG_ARCH_RW;
    for (addr = LOW_MEMORY_START_ADDR; addr < LOW_MEMORY_END_ADDR; addr += PAGE_SIZE) {
        ptd_level_2[VADDR_TO_PTE_INDEX(addr)] = addr | _PG_ARCH_PRESENT | _PG_ARCH_RW;
    }
    ptd_level_1[VADDR_TO_PDE_INDEX(CONFIG_PRTOS_LOAD_ADDR)] = (CONFIG_PRTOS_LOAD_ADDR & LPAGE_MASK) | _PG_ARCH_PRESENT | _PG_ARCH_PSE | _PG_ARCH_RW;
    ptd_level_1[VADDR_TO_PDE_INDEX(CONFIG_PRTOS_OFFSET)] = (CONFIG_PRTOS_LOAD_ADDR & LPAGE_MASK) | _PG_ARCH_PRESENT | _PG_ARCH_PSE | _PG_ARCH_RW;
}
