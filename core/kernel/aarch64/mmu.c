/*
 * FILE: mmu.c
 *
 * AArch64 stage-2 (IPA→PA) MMU setup for PRTOS partitions
 *
 * www.prtos.org
 */

#include <kthread.h>
#include <arch/paging.h>
#include <arch/processor.h>

/*
 * QEMU virt machine: RAM starts at PA 0x40000000.
 * Partitions are configured with IPA addresses (e.g. 0x6000000).
 * Stage-2 translates IPA → PA = IPA + AARCH64_IPA_TO_PA_OFFSET.
 */
#define AARCH64_IPA_TO_PA_OFFSET 0x40000000ULL

/*
 * Xen directmap runtime variables (defined in xen/arch/arm/arm64/mmu/mm.c
 * and xen/arch/arm/mmu/setup.c).  On AArch64 without PDX compression:
 *   EL2 VA = directmap_virt_start - (directmap_base_pdx << PAGE_SHIFT) + PA
 */
extern prtos_u64_t directmap_virt_start;
extern prtos_u64_t directmap_base_pdx;

/*
 * prtos_ipa_to_va - translate a partition IPA to an EL2 hypervisor VA.
 *
 * Uses Xen's directmap which covers all QEMU physical RAM.
 * Called by the generic gp_to_va() macro in gaccess.h so that hypercall
 * handlers can dereference guest-supplied buffer pointers from EL2.
 */
void *prtos_ipa_to_va(prtos_u64_t ipa) {
    prtos_u64_t pa = ipa + AARCH64_IPA_TO_PA_OFFSET;
    return (void *)(directmap_virt_start - (directmap_base_pdx << PAGE_SHIFT) + pa);
}

/*
 * Stage-2 translation: 4KB granule, 32-bit IPA (T0SZ=32, SL0=L1).
 * L1 covers 4 entries × 1GB = 4GB IPA.
 * L2 covers 512 entries × 2MB per L1 entry.
 */
#define S2_L1_ENTRIES 4
#define S2_L2_ENTRIES 512
#define S2_BLOCK_SIZE (2ULL * 1024 * 1024) /* 2MB */
#define S2_BLOCK_MASK (S2_BLOCK_SIZE - 1)

/* Stage-2 descriptor attribute bits (shared between block and page entries) */
#define S2_AF (1ULL << 10)
#define S2_SH_IS (3ULL << 8)
#define S2_S2AP_RW (3ULL << 6)
#define S2_MEMATTR_NORMAL_WB (0xFULL << 2)
#define S2_MEMATTR_DEVICE_nGnRE (0x1ULL << 2)

/* 2MB block descriptor: bits[1:0] = 0b01 */
#define S2_BLOCK_VALID (0x1ULL)
#define S2_BLOCK_ATTRS (S2_AF | S2_SH_IS | S2_S2AP_RW | S2_MEMATTR_NORMAL_WB | S2_BLOCK_VALID)

/* 4KB page descriptor (L3): bits[1:0] = 0b11 */
#define S2_PAGE_VALID (0x3ULL)
#define S2_PAGE_ATTRS (S2_AF | S2_SH_IS | S2_S2AP_RW | S2_MEMATTR_NORMAL_WB | S2_PAGE_VALID)
#define S2_DEVICE_PAGE_ATTRS (S2_AF | S2_SH_IS | S2_S2AP_RW | S2_MEMATTR_DEVICE_nGnRE | S2_PAGE_VALID)

/* Table descriptor (L1→L2 or L2→L3): bits[1:0] = 0b11 */
#define S2_TABLE_VALID (0x3ULL)

#define S2_L3_ENTRIES 512

/*
 * VTCR_EL2 for 4KB granule, 32-bit IPA, 40-bit PA:
 *   T0SZ  [5:0]   = 32  (IPA size = 2^(64-32) = 4GB)
 *   SL0   [7:6]   = 0b01 (start at level 1)
 *   IRGN0 [9:8]   = 0b01 (Normal WB RA WA)
 *   ORGN0 [11:10] = 0b01 (Normal WB RA WA)
 *   SH0   [13:12] = 0b11 (Inner Shareable)
 *   TG0   [15:14] = 0b00 (4KB granule)
 *   PS    [18:16] = 0b010 (40-bit PA)
 *   RES1  [31]    = 1
 */
#define VTCR_EL2_VAL ((1ULL << 31) | (0x2ULL << 16) | (0x3ULL << 12) | (0x1ULL << 10) | (0x1ULL << 8) | (0x1ULL << 6) | 32ULL)

/*
 * get_or_alloc_l3 - Get/allocate an L3 table for a given 2MB block in the L2 table.
 *
 * If the L2 entry is already a table descriptor pointing to an L3, return
 * the L3 pointer.  Otherwise allocate a new L3 table from the pool, install
 * it in the L2 entry, and return the pointer.
 */
static prtos_u64_t *get_or_alloc_l3(kthread_t *k, prtos_u64_t *l2, prtos_s32_t l2_idx) {
    if ((l2[l2_idx] & 0x3) == S2_TABLE_VALID) {
        /* Already a table descriptor → find the L3 table by matching PA */
        prtos_u64_t l3_pa = l2[l2_idx] & ~((1ULL << 12) - 1) & ((1ULL << 48) - 1);
        prtos_s32_t i;
        for (i = 0; i < k->ctrl.g->karch.s2_l3_count; i++) {
            if (_VIRT2PHYS((prtos_u64_t)k->ctrl.g->karch.s2_l3[i]) == l3_pa)
                return k->ctrl.g->karch.s2_l3[i];
        }
    }
    /* Allocate a new L3 table from the pool */
    prtos_s32_t idx = k->ctrl.g->karch.s2_l3_count;
    prtos_u64_t *l3 = k->ctrl.g->karch.s2_l3[idx];
    prtos_s32_t i;
    for (i = 0; i < S2_L3_ENTRIES; i++) l3[i] = 0;
    k->ctrl.g->karch.s2_l3_count++;
    /* Install L2 table descriptor pointing to this L3 */
    l2[l2_idx] = _VIRT2PHYS((prtos_u64_t)l3) | S2_TABLE_VALID;
    return l3;
}

/*
 * setup_stage2_mmu - configure stage-2 IPA→PA translation for a partition
 *
 * Uses L3 (4KB page) mappings for partition memory areas to provide
 * fine-grained memory isolation between partitions.
 *
 * Called from start_up_guest() in kthread.c just before JMP_PARTITION.
 */
void setup_stage2_mmu(kthread_t *k) {
    partition_t *p;
    struct prtos_conf_part *cfg;
    struct prtos_conf_memory_area *areas;
    prtos_u64_t ipa, pa, end_ipa;
    prtos_s32_t l1_idx, l2_idx, l3_idx, area, i;
    prtos_u64_t l2_pa_0, l2_pa_1, pct_pa;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);
    prtos_s32_t vcpu_id = KID2VCPUID(k->ctrl.g->id);

    /*
     * Stage-2 page tables are shared across all vCPUs of the same partition.
     * Only vCPU0 populates the tables; subsequent vCPUs just install VTTBR.
     */
    if (vcpu_id != 0) {
        /* Compute VTTBR from the shared L1 table (already populated by vCPU0) */
        prtos_u64_t vttbr = _VIRT2PHYS((prtos_u64_t)k->ctrl.g->karch.s2_l1);
        vttbr |= ((prtos_u64_t)(part_id + 1) << 48);
        k->ctrl.g->karch.vttbr = vttbr;
        __asm__ __volatile__("msr vtcr_el2, %0\n\t"
                             "msr vttbr_el2, %1\n\t"
                             "dsb ish\n\t"
                             "tlbi alle1is\n\t"
                             "dsb ish\n\t"
                             "isb\n\t"
                             :
                             : "r"((prtos_u64_t)VTCR_EL2_VAL), "r"(vttbr)
                             : "memory");
        return;
    }

    prtos_u64_t *l1 = k->ctrl.g->karch.s2_l1;
    prtos_u64_t *l2_0 = k->ctrl.g->karch.s2_l2[0];
    prtos_u64_t *l2_1 = k->ctrl.g->karch.s2_l2[1];

    /* Zero this partition's tables */
    for (i = 0; i < S2_L1_ENTRIES; i++) l1[i] = 0;
    for (i = 0; i < S2_L2_ENTRIES; i++) {
        l2_0[i] = 0;
        l2_1[i] = 0;
    }
    k->ctrl.g->karch.s2_l3_count = 0;

    /* Physical addresses of this partition's L2 tables */
    l2_pa_0 = _VIRT2PHYS((prtos_u64_t)l2_0);
    l2_pa_1 = _VIRT2PHYS((prtos_u64_t)l2_1);

    /* L1[0] → L2 table 0 (covers IPA 0x0 – 0x3FFFFFFF) */
    l1[0] = l2_pa_0 | S2_TABLE_VALID;
    /* L1[1] → L2 table 1 (covers IPA 0x40000000 – 0x7FFFFFFF) */
    l1[1] = l2_pa_1 | S2_TABLE_VALID;

    /* Map each partition memory area.
     * Use 2MB block descriptors in L2 for large, 2MB-aligned regions (RAM).
     * Fall back to 4KB page descriptors via L3 for unaligned or small regions. */
    p = get_partition(k);
    cfg = p->cfg;
    areas = &prtos_conf_phys_mem_area_table[cfg->physical_memory_areas_offset];

    for (area = 0; area < cfg->num_of_physical_memory_areas; area++) {
        ipa = areas[area].start_addr;
        end_ipa = ipa + areas[area].size;

        /* Fast path: if area is 2MB-aligned and >= 2MB, use L2 block entries */
        if (!(ipa & S2_BLOCK_MASK) && (areas[area].size >= S2_BLOCK_SIZE)) {
            prtos_u64_t block_end = end_ipa & ~S2_BLOCK_MASK;
            prtos_u64_t block_ipa;

            for (block_ipa = ipa; block_ipa < block_end; block_ipa += S2_BLOCK_SIZE) {
                pa = block_ipa + AARCH64_IPA_TO_PA_OFFSET;
                l1_idx = (int)(block_ipa >> 30) & (S2_L1_ENTRIES - 1);
                l2_idx = (int)((block_ipa >> 21) & (S2_L2_ENTRIES - 1));

                prtos_u64_t *l2 = (l1_idx == 0) ? l2_0 : l2_1;
                l2[l2_idx] = (pa & ~S2_BLOCK_MASK) | S2_BLOCK_ATTRS;
            }

            /* Map any remaining tail (< 2MB) using 4KB pages */
            for (ipa = block_end; ipa < end_ipa; ipa += PAGE_SIZE) {
                pa = ipa + AARCH64_IPA_TO_PA_OFFSET;
                l1_idx = (int)(ipa >> 30) & (S2_L1_ENTRIES - 1);
                l2_idx = (int)((ipa >> 21) & (S2_L2_ENTRIES - 1));
                l3_idx = (int)((ipa >> 12) & (S2_L3_ENTRIES - 1));

                prtos_u64_t *l2 = (l1_idx == 0) ? l2_0 : l2_1;
                prtos_u64_t *l3 = get_or_alloc_l3(k, l2, l2_idx);
                l3[l3_idx] = (pa & ~((1ULL << 12) - 1)) | S2_PAGE_ATTRS;
            }
        } else {
            /* Small or unaligned area: use 4KB pages via L3 tables */
            for (; ipa < end_ipa; ipa += PAGE_SIZE) {
                pa = ipa + AARCH64_IPA_TO_PA_OFFSET;
                l1_idx = (int)(ipa >> 30) & (S2_L1_ENTRIES - 1);
                l2_idx = (int)((ipa >> 21) & (S2_L2_ENTRIES - 1));
                l3_idx = (int)((ipa >> 12) & (S2_L3_ENTRIES - 1));

                prtos_u64_t *l2 = (l1_idx == 0) ? l2_0 : l2_1;
                prtos_u64_t *l3 = get_or_alloc_l3(k, l2, l2_idx);
                l3[l3_idx] = (pa & ~((1ULL << 12) - 1)) | S2_PAGE_ATTRS;
            }
        }
    }

    /* Map the entire PCT array (all vCPUs) so every vCPU can access its PCT */
    {
        prtos_u64_t pct_base_pa = _VIRT2PHYS((prtos_u64_t)p->pct_array);
        prtos_u64_t pct_end_pa = pct_base_pa + p->pct_array_size;
        prtos_u64_t pg;
        for (pg = pct_base_pa & ~((prtos_u64_t)PAGE_SIZE - 1); pg < pct_end_pa; pg += PAGE_SIZE) {
            prtos_s32_t pg_l1 = (int)(pg >> 30) & (S2_L1_ENTRIES - 1);
            prtos_s32_t pg_l2 = (int)((pg >> 21) & (S2_L2_ENTRIES - 1));
            prtos_s32_t pg_l3 = (int)((pg >> 12) & (S2_L3_ENTRIES - 1));
            prtos_u64_t *l2 = (pg_l1 == 0) ? l2_0 : l2_1;
            prtos_u64_t *l3 = get_or_alloc_l3(k, l2, pg_l2);
            l3[pg_l3] = (pg & ~((1ULL << 12) - 1)) | S2_PAGE_ATTRS;
        }
    }

    /* Map UART device MMIO (IPA 0x09000000 → PA 0x09000000, identity-mapped) */
    {
        prtos_u64_t uart_ipa = 0x09000000ULL;
        prtos_u64_t uart_pa = 0x09000000ULL;
        prtos_s32_t u_l1 = (int)(uart_ipa >> 30) & (S2_L1_ENTRIES - 1);
        prtos_s32_t u_l2 = (int)((uart_ipa >> 21) & (S2_L2_ENTRIES - 1));
        prtos_s32_t u_l3 = (int)((uart_ipa >> 12) & (S2_L3_ENTRIES - 1));
        prtos_u64_t *l2 = (u_l1 == 0) ? l2_0 : l2_1;
        prtos_u64_t *l3 = get_or_alloc_l3(k, l2, u_l2);
        l3[u_l3] = (uart_pa & ~((1ULL << 12) - 1)) | S2_DEVICE_PAGE_ATTRS;
    }

    /* Install stage-2 page tables */
    prtos_u64_t vttbr = _VIRT2PHYS((prtos_u64_t)l1);
    vttbr |= ((prtos_u64_t)(part_id + 1) << 48);
    k->ctrl.g->karch.vttbr = vttbr;

    __asm__ __volatile__("msr vtcr_el2, %0\n\t"
                         "msr vttbr_el2, %1\n\t"
                         "dsb ish\n\t"
                         "tlbi alle1is\n\t"
                         "dsb ish\n\t"
                         "isb\n\t"
                         :
                         : "r"((prtos_u64_t)VTCR_EL2_VAL), "r"(vttbr)
                         : "memory");
}
