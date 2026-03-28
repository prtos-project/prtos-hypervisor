/*
 * FILE: mmu.c
 *
 * RISC-V 64-bit G-stage (stage-2) page table management
 * Provides memory isolation between partitions via Sv39x4.
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

/* Static pool for G-stage page tables (root=16KB, L1/L0=4KB each) */
#define S2_POOL_SIZE (512 * 1024)
static prtos_u8_t s2_pool[S2_POOL_SIZE] __attribute__((aligned(16384)));
static prtos_u_size_t s2_pool_offset;
static spin_lock_t s2_lock = SPINLOCK_INIT;

static prtos_u64_t *s2_alloc(prtos_u_size_t size, prtos_u_size_t align) {
    prtos_u_size_t off = (s2_pool_offset + align - 1) & ~(align - 1);
    if (off + size > S2_POOL_SIZE) return 0;
    prtos_u64_t *ptr = (prtos_u64_t *)&s2_pool[off];
    s2_pool_offset = off + size;
    memset(ptr, 0, size);
    return ptr;
}

static void map_gstage_page(prtos_u64_t *root, prtos_address_t gpa, prtos_address_t hpa) {
    unsigned int vpn2 = (gpa >> GSTAGE_VPN2_SHIFT) & GSTAGE_VPN2_MASK;
    unsigned int vpn1 = (gpa >> GSTAGE_VPN1_SHIFT) & GSTAGE_VPN1_MASK;
    unsigned int vpn0 = (gpa >> GSTAGE_VPN0_SHIFT) & GSTAGE_VPN0_MASK;
    prtos_u64_t *l1, *l0;

    /* Get or allocate level-1 table */
    if (!(root[vpn2] & PTE_V)) {
        l1 = s2_alloc(GSTAGE_TABLE_SIZE, PAGE_SIZE);
        root[vpn2] = (((prtos_u64_t)(prtos_address_t)l1 >> PAGE_SHIFT) << 10) | PTE_V;
    } else {
        l1 = (prtos_u64_t *)(prtos_address_t)((root[vpn2] >> 10) << PAGE_SHIFT);
    }

    /* Get or allocate level-0 table */
    if (!(l1[vpn1] & PTE_V)) {
        l0 = s2_alloc(GSTAGE_TABLE_SIZE, PAGE_SIZE);
        l1[vpn1] = (((prtos_u64_t)(prtos_address_t)l0 >> PAGE_SHIFT) << 10) | PTE_V;
    } else {
        l0 = (prtos_u64_t *)(prtos_address_t)((l1[vpn1] >> 10) << PAGE_SHIFT);
    }

    /* Set leaf PTE: identity map GPA → HPA */
    l0[vpn0] = ((hpa >> PAGE_SHIFT) << 10) | PTE_LEAF_FLAGS;
}

void setup_stage2_mmu(kthread_t *k) {
    struct prtos_conf_part *cfg;
    struct prtos_conf_memory_area *areas;
    prtos_u64_t *root;
    prtos_s32_t area, part_id;
    prtos_address_t addr, end;
    prtos_word_t flags;

    if (!k->ctrl.g) return;

    spin_lock_irq_save(&s2_lock, flags);

    cfg = get_partition(k)->cfg;
    part_id = KID2PARTID(k->ctrl.g->id);

    /* Allocate 16KB-aligned root table for Sv39x4 */
    root = s2_alloc(GSTAGE_ROOT_SIZE, GSTAGE_ROOT_SIZE);
    if (!root) {
        spin_unlock_irq_restore(&s2_lock, flags);
        return;
    }
    k->ctrl.g->karch.s2_root = root;

    /* Identity-map each memory area assigned to this partition */
    areas = &prtos_conf_phys_mem_area_table[cfg->physical_memory_areas_offset];
    for (area = 0; area < cfg->num_of_physical_memory_areas; area++) {
        addr = areas[area].start_addr;
        end = addr + areas[area].size;
        for (; addr < end; addr += PAGE_SIZE) {
            map_gstage_page(root, addr, addr);
        }
    }

    /* Map the PCT array so every vCPU can access its partition control table */
    {
        partition_t *p = get_partition(k);
        prtos_address_t pct_base = (prtos_address_t)_VIRT2PHYS(p->pct_array);
        prtos_address_t pct_end = pct_base + p->pct_array_size;
        prtos_address_t pg;
        for (pg = pct_base & ~((prtos_address_t)PAGE_SIZE - 1); pg < pct_end; pg += PAGE_SIZE) {
            map_gstage_page(root, pg, pg);
        }
    }

    /* Set hgatp: Sv39x4 mode | VMID | PPN of root table */
    k->ctrl.g->karch.hgatp = HGATP_MODE_SV39X4 |
                     ((prtos_u64_t)(part_id & 0x3FFF) << 44) |
                     ((prtos_u64_t)(prtos_address_t)root >> PAGE_SHIFT);

    spin_unlock_irq_restore(&s2_lock, flags);
}
