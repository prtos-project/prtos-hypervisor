/*
 * FILE: boot.c
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
    prtos_u32_t feat = 0, eax, ebx, ecx, edx;
    prtos_u64_t flags0, flags1;

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

    /* Check extended CPUID for long mode */
    cpu_id(0x80000001, &eax, &ebx, &ecx, &edx);
    if (edx & _CPUID_LM) feat |= _LM_SUPPORT;

    return feat;
}

/*
 * Boot-time page table init for amd64.
 * Sets up 4-level paging: PML4 → PDPT → PD (2MB pages)
 * Identity maps low memory + PRTOS load address,
 * plus the high virtual address mapping.
 */
void __BOOT boot_init_page_table(void) {
    extern prtos_address_t _page_tables[];
    prtos_u64_t *pml4, *pdpt, *pd, *pdpt_hi, *pd_hi;
    prtos_u64_t addr;

    /* _page_tables reserves 6 pages:
     * page 0: PML4
     * page 1: PDPT for identity mapping (low)
     * page 2: PD for identity mapping (low)
     * page 3: PDPT for high virtual mapping
     * page 4: PD for high virtual mapping
     * page 5: reserved for PT (if needed)
     */
    pml4 = (prtos_u64_t *)_VIRT2PHYS(_page_tables);
    pdpt = (prtos_u64_t *)(_VIRT2PHYS(_page_tables) + PAGE_SIZE);
    pd = (prtos_u64_t *)(_VIRT2PHYS(_page_tables) + 2 * PAGE_SIZE);
    pdpt_hi = (prtos_u64_t *)(_VIRT2PHYS(_page_tables) + 3 * PAGE_SIZE);
    pd_hi = (prtos_u64_t *)(_VIRT2PHYS(_page_tables) + 4 * PAGE_SIZE);

    /* Clear all tables */
    for (addr = 0; addr < PAGE_SIZE / 8; addr++) {
        pml4[addr] = 0;
        pdpt[addr] = 0;
        pd[addr] = 0;
        pdpt_hi[addr] = 0;
        pd_hi[addr] = 0;
    }

    /* PML4[0] → PDPT (for low identity mapping) */
    pml4[0] = (prtos_u64_t)pdpt | _PG_ARCH_PRESENT | _PG_ARCH_RW;

    /* PDPT[0] → PD */
    pdpt[0] = (prtos_u64_t)pd | _PG_ARCH_PRESENT | _PG_ARCH_RW;

    /* Identity map the first 1GB using 2MB pages in PD */
    /* Map first 16MB with 2MB pages which covers low memory and PRTOS load address */
    for (addr = 0; addr < 0x1000000; addr += LPAGE_SIZE) {
        pd[addr >> PD_SHIFT] = addr | _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_PSE;
    }
    /* Also map the CONFIG_PRTOS_LOAD_ADDR region */
    addr = CONFIG_PRTOS_LOAD_ADDR & LPAGE_MASK;
    pd[addr >> PD_SHIFT] = addr | _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_PSE;
    /* Map the next 2MB page too, to cover the hypervisor image */
    pd[(addr >> PD_SHIFT) + 1] = (addr + LPAGE_SIZE) | _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_PSE;

    /* Now set up high virtual mapping: CONFIG_PRTOS_OFFSET → CONFIG_PRTOS_LOAD_ADDR */
    /* PML4 entry for CONFIG_PRTOS_OFFSET */
    pml4[PML4_INDEX(CONFIG_PRTOS_OFFSET)] = (prtos_u64_t)pdpt_hi | _PG_ARCH_PRESENT | _PG_ARCH_RW;

    /* PDPT entry */
    pdpt_hi[PDPT_INDEX(CONFIG_PRTOS_OFFSET)] = (prtos_u64_t)pd_hi | _PG_ARCH_PRESENT | _PG_ARCH_RW;

    /* Map physical pages starting at CONFIG_PRTOS_LOAD_ADDR to virtual CONFIG_PRTOS_OFFSET */
    /* Map enough 2MB pages to cover the hypervisor (at least 16MB) */
    {
        prtos_u64_t phys = CONFIG_PRTOS_LOAD_ADDR & LPAGE_MASK;
        prtos_u32_t pd_idx = PD_INDEX(CONFIG_PRTOS_OFFSET);
        prtos_u32_t count;
        for (count = 0; count < 8; count++) {
            pd_hi[pd_idx + count] = phys | _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_PSE | _PG_ARCH_GLOBAL;
            phys += LPAGE_SIZE;
        }
    }
}
