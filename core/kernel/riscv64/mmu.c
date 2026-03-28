/*
 * FILE: mmu.c
 *
 * RISC-V 64-bit G-stage (stage-2) page table management
 * Provides memory isolation between partitions via Sv39x4.
 *
 * Page tables are pre-allocated via GET_MEMAZ in kthread.c and stored
 * in karch fields (s2_root, s2_l1[], s2_l2[]), following the aarch64 pattern.
 *
 * www.prtos.org
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

/* Sv39x4 G-stage page table constants */
#define PTE_V  (1UL << 0)
#define PTE_R  (1UL << 1)
#define PTE_W  (1UL << 2)
#define PTE_X  (1UL << 3)
#define PTE_U  (1UL << 4)
#define PTE_A  (1UL << 6)
#define PTE_D  (1UL << 7)
#define PTE_LEAF_FLAGS (PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D)

#define GSTAGE_VPN2_SHIFT  30   /* bits [40:30] for Sv39x4 root level */
#define GSTAGE_VPN1_SHIFT  21   /* bits [27:21] */
#define GSTAGE_VPN0_SHIFT  12   /* bits [20:12] */
#define GSTAGE_VPN2_MASK   0x7FFUL  /* 11 bits */
#define GSTAGE_VPN1_MASK   0x1FFUL  /* 9 bits */
#define GSTAGE_VPN0_MASK   0x1FFUL  /* 9 bits */

/* Root table: 2048 entries (16KB, 16KB-aligned) */
#define GSTAGE_ROOT_ENTRIES 2048
#define GSTAGE_ROOT_SIZE    (GSTAGE_ROOT_ENTRIES * sizeof(prtos_u64_t))
/* Level-1/0 tables: 512 entries (4KB, 4KB-aligned) */
#define GSTAGE_TABLE_SIZE   (512 * sizeof(prtos_u64_t))

#define HGATP_MODE_SV39X4  (8UL << 60)

void setup_stage2_mmu(kthread_t *k) {
    struct prtos_conf_part *cfg;
    struct prtos_conf_memory_area *areas;
    struct kthread_arch *ka;
    prtos_u64_t *root;
    prtos_s32_t area, part_id;
    prtos_address_t addr, end;
    prtos_word_t flags;
    prtos_s32_t next_l1 = 0;

    if (!k->ctrl.g) return;

    spin_lock_irq_save(&(k->ctrl.lock), flags);

    cfg = get_partition(k)->cfg;
    part_id = KID2PARTID(k->ctrl.g->id);
    ka = &k->ctrl.g->karch;

    /* Root table is pre-allocated via GET_MEMAZ in kthread.c */
    root = ka->s2_root;
    if (!root) {
        spin_unlock_irq_restore(&(k->ctrl.lock), flags);
        return;
    }

    /* Map each memory area.
     * Use 2MB megapages for 2MB-aligned, 2MB-sized chunks.
     * For non-aligned or fractional portions, use 4KB pages via L0 tables. */
    areas = &prtos_conf_phys_mem_area_table[cfg->physical_memory_areas_offset];
    for (area = 0; area < cfg->num_of_physical_memory_areas; area++) {
        addr = areas[area].start_addr;
        end = addr + areas[area].size;

        for (; addr < end; ) {
            unsigned int vpn2 = (addr >> GSTAGE_VPN2_SHIFT) & GSTAGE_VPN2_MASK;
            unsigned int vpn1 = (addr >> GSTAGE_VPN1_SHIFT) & GSTAGE_VPN1_MASK;
            prtos_u64_t *l1;

            /* Get or wire L1 table from pre-allocated karch array */
            if (!(root[vpn2] & PTE_V)) {
                ASSERT(next_l1 < 4);
                l1 = ka->s2_l1[next_l1++];
                root[vpn2] = ((_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l1) >> PAGE_SHIFT) << 10) | PTE_V;
            } else {
                l1 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT((root[vpn2] >> 10) << PAGE_SHIFT);
            }

            /* Use 2MB megapage if addr is 2MB-aligned and at least 2MB remains */
            if (!(addr & (LPAGE_SIZE - 1)) && (end - addr) >= LPAGE_SIZE) {
                l1[vpn1] = ((addr >> PAGE_SHIFT) << 10) | PTE_LEAF_FLAGS;
                addr += LPAGE_SIZE;
            } else {
                /* Use 4KB pages via L0 table */
                unsigned int vpn0 = (addr >> GSTAGE_VPN0_SHIFT) & GSTAGE_VPN0_MASK;
                prtos_u64_t *l0;

                if (l1[vpn1] & (PTE_R | PTE_W | PTE_X)) {
                    /* Megapage → split to L0 */
                    prtos_u64_t mega_pa = (l1[vpn1] >> 10) << PAGE_SHIFT;
                    prtos_s32_t j;
                    ASSERT(ka->s2_l2_count < 8);
                    l0 = ka->s2_l2[ka->s2_l2_count++];
                    for (j = 0; j < 512; j++)
                        l0[j] = (((mega_pa + (prtos_u64_t)j * PAGE_SIZE) >> PAGE_SHIFT) << 10) | PTE_LEAF_FLAGS;
                    l1[vpn1] = ((_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l0) >> PAGE_SHIFT) << 10) | PTE_V;
                } else if (!(l1[vpn1] & PTE_V)) {
                    ASSERT(ka->s2_l2_count < 8);
                    l0 = ka->s2_l2[ka->s2_l2_count++];
                    l1[vpn1] = ((_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l0) >> PAGE_SHIFT) << 10) | PTE_V;
                } else {
                    l0 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT((l1[vpn1] >> 10) << PAGE_SHIFT);
                }

                l0[vpn0] = ((addr >> PAGE_SHIFT) << 10) | PTE_LEAF_FLAGS;
                addr += PAGE_SIZE;
            }
        }
    }

    /* Map the PCT array at 4KB granularity (may not be 2MB-aligned) */
    {
        partition_t *p = get_partition(k);
        prtos_address_t pct_base = (prtos_address_t)_VIRT2PHYS(p->pct_array);
        prtos_address_t pct_end = pct_base + p->pct_array_size;
        prtos_address_t pg;

        for (pg = pct_base & ~((prtos_address_t)PAGE_SIZE - 1); pg < pct_end; pg += PAGE_SIZE) {
            unsigned int vpn2 = (pg >> GSTAGE_VPN2_SHIFT) & GSTAGE_VPN2_MASK;
            unsigned int vpn1 = (pg >> GSTAGE_VPN1_SHIFT) & GSTAGE_VPN1_MASK;
            unsigned int vpn0 = (pg >> GSTAGE_VPN0_SHIFT) & GSTAGE_VPN0_MASK;
            prtos_u64_t *l1, *l0;

            /* Get or wire L1 table */
            if (!(root[vpn2] & PTE_V)) {
                ASSERT(next_l1 < 4);
                l1 = ka->s2_l1[next_l1++];
                root[vpn2] = ((_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l1) >> PAGE_SHIFT) << 10) | PTE_V;
            } else {
                l1 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT((root[vpn2] >> 10) << PAGE_SHIFT);
            }

            /* If L1 entry is a megapage leaf, split into L0 table */
            if (l1[vpn1] & (PTE_R | PTE_W | PTE_X)) {
                /* Megapage → split to 4KB pages via L0 table */
                prtos_u64_t mega_pa = (l1[vpn1] >> 10) << PAGE_SHIFT;
                prtos_s32_t i;

                ASSERT(ka->s2_l2_count < 8);
                l0 = ka->s2_l2[ka->s2_l2_count++];
                for (i = 0; i < 512; i++) {
                    l0[i] = (((mega_pa + (prtos_u64_t)i * PAGE_SIZE) >> PAGE_SHIFT) << 10) | PTE_LEAF_FLAGS;
                }
                l1[vpn1] = ((_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l0) >> PAGE_SHIFT) << 10) | PTE_V;
            } else if (!(l1[vpn1] & PTE_V)) {
                /* No mapping, allocate L0 table */
                ASSERT(ka->s2_l2_count < 8);
                l0 = ka->s2_l2[ka->s2_l2_count++];
                l1[vpn1] = ((_VIRT2PHYS((prtos_u64_t)(prtos_address_t)l0) >> PAGE_SHIFT) << 10) | PTE_V;
            } else {
                l0 = (prtos_u64_t *)(prtos_address_t)_PHYS2VIRT((l1[vpn1] >> 10) << PAGE_SHIFT);
            }

            /* 4KB page entry */
            l0[vpn0] = ((pg >> PAGE_SHIFT) << 10) | PTE_LEAF_FLAGS;
        }
    }

    /* Set hgatp: Sv39x4 mode | VMID | PPN of root table */
    ka->hgatp = HGATP_MODE_SV39X4 |
                ((prtos_u64_t)(part_id & 0x3FFF) << 44) |
                (_VIRT2PHYS((prtos_u64_t)(prtos_address_t)root) >> PAGE_SHIFT);

    spin_unlock_irq_restore(&(k->ctrl.lock), flags);
}
