/*
 * FILE: mmu.c
 *
 * AArch64 Stage-2 page table management
 * Provides memory isolation between partitions via VTTBR_EL2.
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

/* Stage-2 descriptor bits */
#define S2_DESC_VALID  (1UL << 0)
#define S2_DESC_TABLE  (1UL << 1)  /* Table descriptor (non-leaf, L0/L1) */
#define S2_DESC_BLOCK  (0UL << 1)  /* Block descriptor (leaf, L1: 1GB, L2: 2MB) */
#define S2_DESC_PAGE   (1UL << 1)  /* Page descriptor (leaf, L3: 4KB) */

/* Stage-2 memory attributes for normal memory */
#define S2_ATTR_MEM_NORMAL (0xFUL << 2)  /* MemAttr[3:0] = 0b1111 (Normal WB) */
#define S2_ATTR_MEM_DEVICE (0x1UL << 2)  /* MemAttr[3:0] = 0b0001 (Device-nGnRE) */
#define S2_ATTR_AF         (1UL << 10)   /* Access flag */
#define S2_ATTR_SH_INNER   (3UL << 8)    /* Inner Shareable */
#define S2_ATTR_S2AP_RW    (3UL << 6)    /* S2AP: read/write */

#define S2_LEAF_FLAGS (S2_DESC_VALID | S2_DESC_BLOCK | S2_ATTR_MEM_NORMAL | \
                       S2_ATTR_AF | S2_ATTR_SH_INNER | S2_ATTR_S2AP_RW)
#define S2_PAGE_FLAGS (S2_DESC_VALID | S2_DESC_PAGE | S2_ATTR_MEM_NORMAL | \
                       S2_ATTR_AF | S2_ATTR_SH_INNER | S2_ATTR_S2AP_RW)
#define S2_DEVICE_PAGE_FLAGS (S2_DESC_VALID | S2_DESC_PAGE | S2_ATTR_MEM_DEVICE | \
                              S2_ATTR_AF | S2_ATTR_SH_INNER | S2_ATTR_S2AP_RW)

/* Level shifts for 4KB granule, concatenated L1 (IPA bits) */
#define S2_L1_SHIFT  30  /* 1GB per L1 entry */
#define S2_L2_SHIFT  21  /* 2MB per L2 entry */
#define S2_L3_SHIFT  12  /* 4KB per L3 entry */
#define S2_L1_MASK   0x1FFUL  /* 9 bits for L1 index */
#define S2_L2_MASK   0x1FFUL  /* 9 bits for L2 index */
#define S2_L3_MASK   0x1FFUL  /* 9 bits for L3 index */

/* VTTBR_EL2 fields: VMID[63:48] | BADDR[47:1] | CnP[0] */
#define VTTBR_VMID_SHIFT 48

static prtos_u64_t vtcr_cached;

/* VTCR_EL2: T0SZ=25 (39-bit IPA), SL0=01 (start at L1), TG0=00 (4KB) */
static void setup_vtcr(void) {
    prtos_u64_t vtcr;
    vtcr = 25;            /* T0SZ = 25 → 39-bit IPA */
    vtcr |= (1UL << 6);  /* SL0 = 01 (start at level 1) */
    vtcr |= (1UL << 8);  /* IRGN0 = 01 (WB RA WA) */
    vtcr |= (1UL << 10); /* ORGN0 = 01 (WB RA WA) */
    vtcr |= (3UL << 12); /* SH0 = 11 (Inner Shareable) */
    vtcr |= (2UL << 16); /* PS = 010 (40-bit PA) */
    vtcr |= (1UL << 31); /* RES1 */
    vtcr_cached = vtcr;
    __asm__ __volatile__("msr vtcr_el2, %0\n\tisb" : : "r"(vtcr));
}

/* Called from setup_arch_local on secondary CPUs to replicate VTCR_EL2 */
void setup_vtcr_percpu(void) {
    prtos_u64_t vtcr;
    vtcr = 25;            /* T0SZ = 25 → 39-bit IPA */
    vtcr |= (1UL << 6);  /* SL0 = 01 (start at level 1) */
    vtcr |= (1UL << 8);  /* IRGN0 = 01 (WB RA WA) */
    vtcr |= (1UL << 10); /* ORGN0 = 01 (WB RA WA) */
    vtcr |= (3UL << 12); /* SH0 = 11 (Inner Shareable) */
    vtcr |= (2UL << 16); /* PS = 010 (40-bit PA) */
    vtcr |= (1UL << 31); /* RES1 */
    __asm__ __volatile__("msr vtcr_el2, %0\n\tisb" : : "r"(vtcr));
}

/* IPA to PA offset: QEMU virt RAM starts at 0x40000000, but partition IPAs
 * are configured starting from 0. The RSW loads partition data at PA = IPA + offset.
 * The stage-2 must apply the same offset. */
#define S2_IPA_TO_PA_OFFSET 0x40000000ULL

void setup_stage2_mmu(kthread_t *k) {
    struct prtos_conf_part *cfg;
    struct prtos_conf_memory_area *areas;
    struct kthread_arch *ka;
    prtos_u64_t *root;
    prtos_s32_t area, part_id, vcpu_id;
    prtos_address_t addr, end;
    prtos_word_t flags;
    prtos_s32_t next_l2 = 0;
    static int vtcr_done = 0;

    if (!k->ctrl.g) return;

    if (!vtcr_done) {
        setup_vtcr();
        vtcr_done = 1;
    }

    cfg = get_partition(k)->cfg;
    part_id = KID2PARTID(k->ctrl.g->id);
    vcpu_id = KID2VCPUID(k->ctrl.g->id);
    ka = &k->ctrl.g->karch;

    root = ka->s2_l1;
    if (!root) return;

    /*
     * Stage-2 page tables are shared across all vCPUs of the same partition.
     * Only vCPU0 populates the tables; subsequent vCPUs just install VTTBR
     * using vCPU0's root page table.
     */
    if (vcpu_id != 0) {
        partition_t *p = get_partition(k);
        prtos_u64_t *vcpu0_root = p->kthread[0]->ctrl.g->karch.s2_l1;
        ka->vttbr_el2 = ((prtos_u64_t)((part_id + 1) & 0xFF) << VTTBR_VMID_SHIFT) |
                         (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)vcpu0_root) & ~0xFFFULL);
        return;
    }

    spin_lock_irq_save(&(k->ctrl.lock), flags);

    /* Map each memory area */
    areas = &prtos_conf_phys_mem_area_table[cfg->physical_memory_areas_offset];
    for (area = 0; area < cfg->num_of_physical_memory_areas; area++) {
        addr = areas[area].start_addr;
        end = addr + areas[area].size;

        for (; addr < end; ) {
            prtos_u64_t pa = addr + S2_IPA_TO_PA_OFFSET;
            unsigned int l1_idx = (addr >> S2_L1_SHIFT) & S2_L1_MASK;
            unsigned int l2_idx = (addr >> S2_L2_SHIFT) & S2_L2_MASK;
            prtos_u64_t *l1 = root;  /* SL0=1, root IS the L1 table */
            prtos_u64_t *l2;

            /* Get or create L2 table */
            if (!(l1[l1_idx] & S2_DESC_VALID)) {
                ASSERT(next_l2 < 2);
                l2 = ka->s2_l2[next_l2++];
                l1[l1_idx] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l2) & ~0xFFFULL) | S2_DESC_VALID | S2_DESC_TABLE;
            } else {
                l2 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT(l1[l1_idx] & ~0xFFFULL);
            }

            /* Use 2MB block if aligned and enough space */
            if (!(addr & (LPAGE_SIZE - 1)) && (end - addr) >= LPAGE_SIZE) {
                l2[l2_idx] = (pa & ~((prtos_u64_t)LPAGE_SIZE - 1)) | S2_LEAF_FLAGS;
                addr += LPAGE_SIZE;
            } else {
                /* 4KB pages via L3 table */
                unsigned int l3_idx = (addr >> S2_L3_SHIFT) & S2_L3_MASK;
                prtos_u64_t *l3;

                if (l2[l2_idx] & (S2_ATTR_S2AP_RW)) {
                    /* Block -> split to L3 */
                    prtos_u64_t block_pa = l2[l2_idx] & ~((prtos_u64_t)LPAGE_SIZE - 1);
                    prtos_s32_t j;
                    ASSERT(ka->s2_l3_count < 8);
                    l3 = ka->s2_l3[ka->s2_l3_count++];
                    for (j = 0; j < 512; j++)
                        l3[j] = (block_pa + (prtos_u64_t)j * PAGE_SIZE) | S2_PAGE_FLAGS;
                    l2[l2_idx] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l3) & ~0xFFFULL) | S2_DESC_VALID | S2_DESC_TABLE;
                } else if (!(l2[l2_idx] & S2_DESC_VALID)) {
                    ASSERT(ka->s2_l3_count < 8);
                    l3 = ka->s2_l3[ka->s2_l3_count++];
                    l2[l2_idx] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l3) & ~0xFFFULL) | S2_DESC_VALID | S2_DESC_TABLE;
                } else {
                    l3 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT(l2[l2_idx] & ~0xFFFULL);
                }

                l3[l3_idx] = (pa & ~((prtos_u64_t)PAGE_SIZE - 1)) | S2_PAGE_FLAGS;
                addr += PAGE_SIZE;
            }
        }
    }

    /* Map the PCT array at 4KB granularity */
    {
        partition_t *p = get_partition(k);
        prtos_address_t pct_base = (prtos_address_t)_VIRT2PHYS(p->pct_array);
        prtos_address_t pct_end = pct_base + p->pct_array_size;
        prtos_address_t pg;

        for (pg = pct_base & ~((prtos_address_t)PAGE_SIZE - 1); pg < pct_end; pg += PAGE_SIZE) {
            unsigned int l1_idx = (pg >> S2_L1_SHIFT) & S2_L1_MASK;
            unsigned int l2_idx = (pg >> S2_L2_SHIFT) & S2_L2_MASK;
            unsigned int l3_idx = (pg >> S2_L3_SHIFT) & S2_L3_MASK;
            prtos_u64_t *l1 = root;
            prtos_u64_t *l2, *l3;

            if (!(l1[l1_idx] & S2_DESC_VALID)) {
                ASSERT(next_l2 < 2);
                l2 = ka->s2_l2[next_l2++];
                l1[l1_idx] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l2) & ~0xFFFULL) | S2_DESC_VALID | S2_DESC_TABLE;
            } else {
                l2 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT(l1[l1_idx] & ~0xFFFULL);
            }

            if (l2[l2_idx] & (S2_ATTR_S2AP_RW) & ~S2_DESC_TABLE) {
                /* Block -> split */
                prtos_u64_t block_pa = l2[l2_idx] & ~((prtos_u64_t)LPAGE_SIZE - 1);
                prtos_s32_t i;
                ASSERT(ka->s2_l3_count < 8);
                l3 = ka->s2_l3[ka->s2_l3_count++];
                for (i = 0; i < 512; i++)
                    l3[i] = (block_pa + (prtos_u64_t)i * PAGE_SIZE) | S2_PAGE_FLAGS;
                l2[l2_idx] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l3) & ~0xFFFULL) | S2_DESC_VALID | S2_DESC_TABLE;
            } else if (!(l2[l2_idx] & S2_DESC_VALID)) {
                ASSERT(ka->s2_l3_count < 8);
                l3 = ka->s2_l3[ka->s2_l3_count++];
                l2[l2_idx] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l3) & ~0xFFFULL) | S2_DESC_VALID | S2_DESC_TABLE;
            } else {
                l3 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT(l2[l2_idx] & ~0xFFFULL);
            }

            l3[l3_idx] = (pg & ~((prtos_u64_t)PAGE_SIZE - 1)) | S2_PAGE_FLAGS;
        }
    }

    /* Map UART device MMIO (IPA 0x09000000 → PA 0x09000000, identity-mapped) */
    {
        prtos_u64_t uart_ipa = 0x09000000ULL;
        unsigned int u_l1_idx = (uart_ipa >> S2_L1_SHIFT) & S2_L1_MASK;
        unsigned int u_l2_idx = (uart_ipa >> S2_L2_SHIFT) & S2_L2_MASK;
        unsigned int u_l3_idx = (uart_ipa >> S2_L3_SHIFT) & S2_L3_MASK;
        prtos_u64_t *l1 = root;
        prtos_u64_t *l2, *l3;

        if (!(l1[u_l1_idx] & S2_DESC_VALID)) {
            ASSERT(next_l2 < 2);
            l2 = ka->s2_l2[next_l2++];
            l1[u_l1_idx] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l2) & ~0xFFFULL) | S2_DESC_VALID | S2_DESC_TABLE;
        } else {
            l2 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT(l1[u_l1_idx] & ~0xFFFULL);
        }

        if (!(l2[u_l2_idx] & S2_DESC_VALID)) {
            ASSERT(ka->s2_l3_count < 8);
            l3 = ka->s2_l3[ka->s2_l3_count++];
            l2[u_l2_idx] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l3) & ~0xFFFULL) | S2_DESC_VALID | S2_DESC_TABLE;
        } else if (l2[u_l2_idx] & S2_DESC_TABLE) {
            l3 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT(l2[u_l2_idx] & ~0xFFFULL);
        } else {
            /* L2 is a block entry; split it to L3 */
            prtos_u64_t block_pa = l2[u_l2_idx] & ~((prtos_u64_t)LPAGE_SIZE - 1);
            prtos_s32_t j;
            ASSERT(ka->s2_l3_count < 8);
            l3 = ka->s2_l3[ka->s2_l3_count++];
            for (j = 0; j < 512; j++)
                l3[j] = (block_pa + (prtos_u64_t)j * PAGE_SIZE) | S2_PAGE_FLAGS;
            l2[u_l2_idx] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l3) & ~0xFFFULL) | S2_DESC_VALID | S2_DESC_TABLE;
        }

        l3[u_l3_idx] = (uart_ipa & ~((prtos_u64_t)PAGE_SIZE - 1)) | S2_DEVICE_PAGE_FLAGS;
    }

    /* Map GIC MMIO for para-virt partitions (vgic == NULL) so they can
     * directly access GIC hardware for interrupt configuration.
     * Hw-virt partitions (vgic != NULL) trap GIC accesses via stage-2
     * data aborts to the MMIO emulation layer.
     * GICD: 0x08000000, GICR: 0x080A0000+, total ~3MB.
     * Map two 2MB L2 blocks covering 0x08000000-0x083FFFFF. */
    if (!ka->vgic) {
        unsigned int gic_l1_idx = (0x08000000ULL >> S2_L1_SHIFT) & S2_L1_MASK;
        prtos_u64_t *gic_l2;

        if (!(root[gic_l1_idx] & S2_DESC_VALID)) {
            ASSERT(next_l2 < 2);
            gic_l2 = ka->s2_l2[next_l2++];
            root[gic_l1_idx] = (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)gic_l2) & ~0xFFFULL) | S2_DESC_VALID | S2_DESC_TABLE;
        } else {
            gic_l2 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT(root[gic_l1_idx] & ~0xFFFULL);
        }

        /* L2[64] = 0x08000000-0x081FFFFF (GICD + GICR start) */
        unsigned int gic_l2_idx0 = (0x08000000ULL >> S2_L2_SHIFT) & S2_L2_MASK;
        gic_l2[gic_l2_idx0] = (0x08000000ULL & ~((prtos_u64_t)LPAGE_SIZE - 1)) |
                                S2_DESC_VALID | S2_DESC_BLOCK | S2_ATTR_MEM_DEVICE |
                                S2_ATTR_AF | S2_ATTR_SH_INNER | S2_ATTR_S2AP_RW;
        /* L2[65] = 0x08200000-0x083FFFFF (GICR continuation) */
        unsigned int gic_l2_idx1 = (0x08200000ULL >> S2_L2_SHIFT) & S2_L2_MASK;
        gic_l2[gic_l2_idx1] = (0x08200000ULL & ~((prtos_u64_t)LPAGE_SIZE - 1)) |
                                S2_DESC_VALID | S2_DESC_BLOCK | S2_ATTR_MEM_DEVICE |
                                S2_ATTR_AF | S2_ATTR_SH_INNER | S2_ATTR_S2AP_RW;
    } else {
        /* For hw-virt partitions, ensure GIC MMIO ranges within the partition
         * memory are unmapped so accesses trap to vGIC emulation.
         * Partition memory mapping may have created 2MB block entries covering
         * GICD/GICC/GICR at 0x08000000-0x083FFFFF. Invalidate those entries. */
        unsigned int gic_l1_idx = (0x08000000ULL >> S2_L1_SHIFT) & S2_L1_MASK;
        if (root[gic_l1_idx] & S2_DESC_VALID) {
            prtos_u64_t *gic_l2 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT(root[gic_l1_idx] & ~0xFFFULL);
            /* L2[64]: 0x08000000-0x081FFFFF (GICD, GICC, GICR start) */
            unsigned int gic_l2_idx0 = (0x08000000ULL >> S2_L2_SHIFT) & S2_L2_MASK;
            gic_l2[gic_l2_idx0] = 0;
            /* L2[65]: 0x08200000-0x083FFFFF (GICR continuation) */
            unsigned int gic_l2_idx1 = (0x08200000ULL >> S2_L2_SHIFT) & S2_L2_MASK;
            gic_l2[gic_l2_idx1] = 0;
        }
    }

    /* Set VTTBR_EL2: VMID | BADDR (VMID = part_id+1, 0 reserved for hypervisor) */
    ka->vttbr_el2 = ((prtos_u64_t)((part_id + 1) & 0xFF) << VTTBR_VMID_SHIFT) |
                     (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)root) & ~0xFFFULL);

    spin_unlock_irq_restore(&(k->ctrl.lock), flags);
}
