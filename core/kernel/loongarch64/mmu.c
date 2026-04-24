/*
 * FILE: mmu.c
 *
 * LoongArch 64-bit guest memory management for PRTOS partitions.
 *
 * Para-virtualization mode: partitions run at PLV3 in PG mode.
 * The hypervisor fills TLB entries for each partition's physical
 * memory areas before entering the partition.  Identity mapping
 * (VA == PA) is used so that the RSW-loaded code/data can execute
 * at the addresses where it was placed.
 *
 * Memory isolation: A per-CPU TLB refill lookup table (tlb_refill_table)
 * stores the TLBELO value for each 1MB page in the first 1GB of physical
 * memory.  Unauthorized pages have entry 0 (V=0), causing Page Invalid
 * exceptions on access.  The TLB refill handler in entry.S reads this
 * table to determine correct flags for each page independently.
 *
 * http://www.prtos.org/
 */

#include <assert.h>
#include <kthread.h>
#include <rsvmem.h>
#include <stdc.h>
#include <guest.h>
#include <prtosconf.h>
#include <spinlock.h>
#include <arch/paging.h>
#include <arch/processor.h>

/* LoongArch TLB CSR numbers */
#define CSR_TLBIDX   0x10
#define CSR_TLBEHI   0x11
#define CSR_TLBELO0  0x12
#define CSR_TLBELO1  0x13
#define CSR_ASID     0x18
#define CSR_STLBPS   0x1E

/* TLBELO flags */
#define TLBELO_V     (1ULL << 0)   /* Valid */
#define TLBELO_D     (1ULL << 1)   /* Dirty (writable) */
#define TLBELO_PLV3  (3ULL << 2)   /* PLV=3, accessible from all levels */
#define TLBELO_CC    (1ULL << 4)   /* Cached Coherent (MAT=1) */
#define TLBELO_G     (1ULL << 6)   /* Global (ignore ASID) */

#define TLBELO_FLAGS_PART (TLBELO_V | TLBELO_D | TLBELO_PLV3 | TLBELO_CC | TLBELO_G)
/* Read-only flags for hypervisor memory accessible from PLV3 (no D bit) */
#define TLBELO_FLAGS_HYP_RO (TLBELO_V | TLBELO_PLV3 | TLBELO_CC | TLBELO_G)

/* TLBIDX PS field is bits [29:24] */
#define TLBIDX_PS(ps) ((prtos_u64_t)(ps) << 24)

#define PAGE_SIZE_1M  20

/*
 * Per-CPU TLB refill lookup table.
 * Index: 1MB page number (0..1023 for first 1GB of physical memory)
 * Value: Complete TLBELO value (PA | flags), or 0 if page is not accessible
 *        from PLV3 (partition mode).
 *
 * The TLB refill handler (entry.S) reads this table to determine the
 * correct TLBELO0/TLBELO1 values for each 1MB even/odd page pair.
 * This provides per-page (1MB granularity) memory isolation between partitions.
 */
#define TLB_TABLE_ENTRIES 4096
prtos_u64_t tlb_refill_table[CONFIG_NO_CPUS][TLB_TABLE_ENTRIES]
    __attribute__((aligned(64)));

/*
 * Update the per-CPU TLB refill table for the given partition's kthread.
 * Called during partition context switch before entering the partition.
 */
static void update_tlb_refill_table(kthread_t *k) {
    prtos_s32_t cpu_id = GET_CPU_ID();
    struct prtos_conf_part *cfg = get_partition(k)->cfg;
    struct prtos_conf_memory_area *areas =
        &prtos_conf_phys_mem_area_table[cfg->physical_memory_areas_offset];
    prtos_s32_t area, idx;

    /* Clear entire table (all pages inaccessible from PLV3) */
    memset(tlb_refill_table[cpu_id], 0, sizeof(tlb_refill_table[cpu_id]));

    /* Map hypervisor first 2MB as read-only for PLV3.
     * This covers PCT, prtos_conf, and hypervisor code/BSS. */
    {
        prtos_s32_t hyp_start_idx = CONFIG_PRTOS_LOAD_ADDR >> 20; /* 0x04000000 >> 20 = 64 */
        for (idx = hyp_start_idx; idx < hyp_start_idx + 2 && idx < TLB_TABLE_ENTRIES; idx++) {
            tlb_refill_table[cpu_id][idx] = ((prtos_u64_t)idx << 20) | TLBELO_FLAGS_HYP_RO;
        }
    }

/* TLBELO flags for partition pages: uncacheable (MAT=0 SUC) */
#define TLBELO_FLAGS_PART_UC (TLBELO_V | TLBELO_D | TLBELO_PLV3 | TLBELO_G)

    /* Map partition's physical memory areas */
    for (area = 0; area < cfg->num_of_physical_memory_areas; area++) {
        prtos_address_t start = areas[area].start_addr;
        prtos_address_t size = areas[area].size;
        prtos_s32_t start_idx = start >> 20;
        prtos_s32_t end_idx = (start + size + (1ULL << 20) - 1) >> 20;
        prtos_u64_t elo_flags = TLBELO_FLAGS_PART;
        if (areas[area].flags & PRTOS_MEM_AREA_READONLY) {
            elo_flags = TLBELO_FLAGS_HYP_RO; /* D=0 */
        }
        if (areas[area].flags & PRTOS_MEM_AREA_UNCACHEABLE) {
            elo_flags = TLBELO_FLAGS_PART_UC; /* MAT=0 (SUC), no CC bit */
        }
        for (idx = start_idx; idx < end_idx && idx < TLB_TABLE_ENTRIES; idx++) {
            tlb_refill_table[cpu_id][idx] = ((prtos_u64_t)idx << 20) | elo_flags;
        }
    }

    /* Ensure PCT page is mapped (read-only) if not already covered */
    {
        prtos_address_t pct_pa = _VIRT2PHYS((prtos_address_t)k->ctrl.g->part_ctrl_table);
        prtos_s32_t pct_idx = pct_pa >> 20;
        if (pct_idx >= 0 && pct_idx < TLB_TABLE_ENTRIES) {
            if (tlb_refill_table[cpu_id][pct_idx] == 0) {
                tlb_refill_table[cpu_id][pct_idx] =
                    ((prtos_u64_t)pct_idx << 20) | TLBELO_FLAGS_HYP_RO;
            }
        }
    }
}

/*
 * Pre-fill TLB entries from the refill table for valid pages.
 * Reduces TLB miss overhead on first access after partition switch.
 */
static void prefill_tlb_from_table(prtos_s32_t cpu_id) {
    prtos_s32_t idx;
    for (idx = 0; idx < TLB_TABLE_ENTRIES; idx += 2) {
        prtos_u64_t elo0 = tlb_refill_table[cpu_id][idx];
        prtos_u64_t elo1 = tlb_refill_table[cpu_id][idx + 1];
        if (!elo0 && !elo1) continue;

        prtos_u64_t pair_base = (prtos_u64_t)idx << 20;
        prtos_u64_t tlbehi = pair_base;
        prtos_u64_t tlbidx = TLBIDX_PS(PAGE_SIZE_1M);

        __asm__ __volatile__(
            "csrwr %0, 0x11\n\t"   /* TLBEHI */
            "csrwr %1, 0x12\n\t"   /* TLBELO0 */
            "csrwr %2, 0x13\n\t"   /* TLBELO1 */
            "csrwr %3, 0x10\n\t"   /* TLBIDX */
            "tlbfill\n\t"
            :
            : "r"(tlbehi), "r"(elo0), "r"(elo1), "r"(tlbidx)
            : "memory"
        );
    }
}

/*
 * Set up TLB entries for the partition's memory areas.
 * Called before JMP_PARTITION to ensure the partition can access
 * its memory at PLV3.
 */
void setup_stage2_mmu(kthread_t *k) {
    prtos_word_t flags;

    if (!k->ctrl.g) return;

    spin_lock_irq_save(&(k->ctrl.lock), flags);

    /* Update per-CPU TLB refill table for this partition */
    update_tlb_refill_table(k);

    /* Store physical address of per-CPU table in CSR_SAVE4 (0x34).
     * The TLB refill handler reads this to find the table without
     * needing la.pcrel (which requires matching VA/PA layouts). */
    {
        prtos_s32_t cpu_id = GET_CPU_ID();
        prtos_u64_t table_pa = _VIRT2PHYS((prtos_address_t)&tlb_refill_table[cpu_id][0]);
        __asm__ __volatile__("csrwr %0, 0x34" : "+r"(table_pa)); /* CSR_SAVE4 */
    }

    /* Flush all TLB entries to remove stale mappings from previous partition */
    __asm__ __volatile__("invtlb 0x0, $zero, $zero" ::: "memory");

    /* TLB refill handler will fill entries from the table on demand */

    spin_unlock_irq_restore(&(k->ctrl.lock), flags);
}
