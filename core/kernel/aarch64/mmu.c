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

/* Maximum number of partitions with independent stage-2 page tables */
#define MAX_S2_PARTITIONS 4

/* 2MB block stage-2 descriptor attributes:
 *   bits[1:0]  = 0b01  block entry
 *   bits[5:2]  = 0b1111 MemAttr: Normal WB (outer=0b11, inner=0b11)
 *   bits[7:6]  = 0b11  S2AP: R/W
 *   bits[9:8]  = 0b11  SH: Inner Shareable
 *   bit [10]   = 1     AF: Access Flag (avoid AF fault)
 */
#define S2_BLOCK_AF (1ULL << 10)
#define S2_BLOCK_SH_IS (3ULL << 8)
#define S2_BLOCK_S2AP_RW (3ULL << 6)
#define S2_BLOCK_MEMATTR_NORMAL_WB (0xFULL << 2)
#define S2_BLOCK_VALID (0x1ULL)
#define S2_BLOCK_ATTRS (S2_BLOCK_AF | S2_BLOCK_SH_IS | S2_BLOCK_S2AP_RW | S2_BLOCK_MEMATTR_NORMAL_WB | S2_BLOCK_VALID)

/* L1 table descriptor pointing to an L2 table:
 *   bits[1:0] = 0b11 (table descriptor)
 *   bits[47:12] = L2 physical base address
 */
#define S2_TABLE_VALID (0x3ULL)

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

/* Per-partition static page tables in BSS (4KB aligned).
 * Each partition gets its own L1, L2_0, L2_1 tables indexed by partition ID. */
static prtos_u64_t s2_l1[MAX_S2_PARTITIONS][S2_L1_ENTRIES] __attribute__((aligned(4096)));
static prtos_u64_t s2_l2_0[MAX_S2_PARTITIONS][S2_L2_ENTRIES] __attribute__((aligned(4096)));
static prtos_u64_t s2_l2_1[MAX_S2_PARTITIONS][S2_L2_ENTRIES] __attribute__((aligned(4096)));

/*
 * setup_stage2_mmu - configure stage-2 IPA→PA translation for a partition
 *
 * Maps all partition physical memory areas: IPA (start_addr) → PA (start_addr + offset).
 * Installs VTCR_EL2 and VTTBR_EL2, then enables stage-2 via HCR_EL2.VM.
 * Stores the VTTBR value in karch.vttbr for context switch restoration.
 *
 * Called from start_up_guest() in kthread.c just before JMP_PARTITION.
 */
void setup_stage2_mmu(kthread_t *k) {
    partition_t *p;
    struct prtos_conf_part *cfg;
    struct prtos_conf_memory_area *areas;
    prtos_u64_t block_desc;
    prtos_u64_t ipa, pa, end_ipa;
    prtos_s32_t l1_idx, l2_idx, area;
    prtos_u64_t l2_pa_0, l2_pa_1, pct_pa;
    int pct_l1_idx, pct_l2_idx;
    prtos_s32_t part_id = KID2PARTID(k->ctrl.g->id);

    if (part_id >= MAX_S2_PARTITIONS) return;

    /* Zero this partition's tables */
    for (l1_idx = 0; l1_idx < S2_L1_ENTRIES; l1_idx++) s2_l1[part_id][l1_idx] = 0;
    for (l2_idx = 0; l2_idx < S2_L2_ENTRIES; l2_idx++) {
        s2_l2_0[part_id][l2_idx] = 0;
        s2_l2_1[part_id][l2_idx] = 0;
    }

    /* Physical addresses of this partition's L2 tables */
    l2_pa_0 = _VIRT2PHYS((prtos_u64_t)s2_l2_0[part_id]);
    l2_pa_1 = _VIRT2PHYS((prtos_u64_t)s2_l2_1[part_id]);

    /* L1[0] → L2 table 0 (covers IPA 0x0 – 0x3FFFFFFF, 1GB window) */
    s2_l1[part_id][0] = l2_pa_0 | S2_TABLE_VALID;
    /* L1[1] → L2 table 1 (covers IPA 0x40000000 – 0x7FFFFFFF, 1GB window for PCT) */
    s2_l1[part_id][1] = l2_pa_1 | S2_TABLE_VALID;

    /* Map each partition memory area as 2MB blocks */
    p = get_partition(k);
    cfg = p->cfg;
    areas = &prtos_conf_phys_mem_area_table[cfg->physical_memory_areas_offset];

    for (area = 0; area < cfg->num_of_physical_memory_areas; area++) {
        ipa = areas[area].start_addr;
        end_ipa = ipa + areas[area].size;
        pa = ipa + AARCH64_IPA_TO_PA_OFFSET;

        /* Align to 2MB boundary and map in 2MB blocks */
        for (; ipa < end_ipa; ipa += (1ULL << 21), pa += (1ULL << 21)) {
            l1_idx = (int)(ipa >> 30) & (S2_L1_ENTRIES - 1);
            l2_idx = (int)((ipa >> 21) & (S2_L2_ENTRIES - 1));
            block_desc = (pa & ~((1ULL << 21) - 1)) | S2_BLOCK_ATTRS;
            if (l1_idx == 0)
                s2_l2_0[part_id][l2_idx] = block_desc;
            else if (l1_idx == 1)
                s2_l2_1[part_id][l2_idx] = block_desc;
        }
    }

    /* Map the PCT with identity IPA=PA so the partition can access it.
     * PCT is in PRTOS BSS (phys 0x49000000+), falling in L1[1] (0x40000000-0x7FFFFFFF). */
    pct_pa = _VIRT2PHYS((prtos_u64_t)k->ctrl.g->part_ctrl_table);
    pct_l1_idx = (int)(pct_pa >> 30) & (S2_L1_ENTRIES - 1);
    pct_l2_idx = (int)((pct_pa >> 21) & (S2_L2_ENTRIES - 1));
    block_desc = (pct_pa & ~((1ULL << 21) - 1)) | S2_BLOCK_ATTRS;
    if (pct_l1_idx == 0)
        s2_l2_0[part_id][pct_l2_idx] = block_desc;
    else if (pct_l1_idx == 1)
        s2_l2_1[part_id][pct_l2_idx] = block_desc;

    /* Install stage-2 page tables */
    prtos_u64_t vttbr = _VIRT2PHYS((prtos_u64_t)s2_l1[part_id]);
    /* VMID = part_id + 1 (VMID 0 reserved for hypervisor) */
    vttbr |= ((prtos_u64_t)(part_id + 1) << 48);

    /* Store VTTBR for context switch restoration */
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
