/*
 * FILE: head.S
 *
 * RISC-V 64-bit Sv39 early page table setup
 * Creates initial mapping to enable the MMU before jumping to virtual addresses.
 *
 * Sv39: 3-level page tables, 512 entries per level, 4KB pages / 2MB megapages / 1GB gigapages.
 * VPN[2] = VA[38:30], VPN[1] = VA[29:21], VPN[0] = VA[20:12]
 *
 * We use 1GB gigapage entries (level-1 leaf) for simplicity:
 *   - Identity map: VA == PA for the 1GB region containing CONFIG_PRTOS_LOAD_ADDR
 *   - Virtual map:  VA starting at CONFIG_PRTOS_OFFSET → same physical 1GB region
 *
 * After the kernel transitions to virtual addresses, the identity map is removed.
 *
 * www.prtos.org
 */

#include <linkage.h>
#include <arch/paging.h>
#include <arch/prtos_def.h>

/* PTE flags for leaf entries (gigapage) */
#define PTE_V     0x001
#define PTE_R     0x002
#define PTE_W     0x004
#define PTE_X     0x008
#define PTE_G     0x020
#define PTE_A     0x040
#define PTE_D     0x080
#define LEAF_FLAGS (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)

/* Sv39 constants */
#define SV39_GIGAPAGE_SHIFT 30     /* 1GB per level-1 entry */
#define SATP_SV39_MODE      0x8000000000000000

.section .boot.text, "ax"
.option norelax

/*
 * setup_boot_page_table - create initial Sv39 page table for boot
 *
 * Uses _boot_pt_l1 (a 4KB-aligned, zeroed page in .boot.data) as the root (L1)
 * page table. Creates two mappings using 1GB gigapages:
 *
 *   1) Identity map: VPN[2] for CONFIG_PRTOS_LOAD_ADDR → PA gigapage base
 *   2) Virtual  map: VPN[2] for CONFIG_PRTOS_OFFSET    → same PA gigapage base
 *
 * Clobbers: t0-t5
 */
.globl setup_boot_page_table
.type setup_boot_page_table, @function
setup_boot_page_table:
    /* t0 = physical address of root page table */
    lla  t0, _boot_pt_l1

    /* Calculate the 1GB-aligned physical base for the region containing LOAD_ADDR */
    li   t1, CONFIG_PRTOS_LOAD_ADDR
    li   t2, (1 << SV39_GIGAPAGE_SHIFT)
    /* t3 = gigapage-aligned base = LOAD_ADDR & ~(1GB-1) */
    addi t5, t2, -1       /* t5 = 0x3FFFFFFF */
    not  t5, t5            /* t5 = ~0x3FFFFFFF */
    and  t3, t1, t5        /* t3 = PA base */

    /* Build PTE: (PA_base >> 12) << 10 | LEAF_FLAGS */
    srli t4, t3, 12
    slli t4, t4, 10
    li   t5, LEAF_FLAGS
    or   t4, t4, t5       /* t4 = PTE value */

    /* 1) Identity map: VPN[2] = (LOAD_ADDR >> 30) & 0x1FF */
    srli t1, t1, SV39_GIGAPAGE_SHIFT
    andi t1, t1, 0x1FF
    slli t1, t1, 3        /* byte offset in table (entry * 8) */
    add  t1, t0, t1
    sd   t4, 0(t1)        /* store PTE */

    /* 2) Virtual map: VPN[2] = (PRTOS_OFFSET >> 30) & 0x1FF */
    li   t1, CONFIG_PRTOS_OFFSET
    srli t1, t1, SV39_GIGAPAGE_SHIFT
    andi t1, t1, 0x1FF
    slli t1, t1, 3
    add  t1, t0, t1
    sd   t4, 0(t1)        /* store PTE */

    /*
     * 3) Identity map MMIO region: VPN[2]=0 → PA 0x00000000 (UART, PLIC, CLINT live here)
     * Build PTE for 0x00000000: (0 >> 12) << 10 | LEAF_FLAGS = LEAF_FLAGS
     */
    li   t4, LEAF_FLAGS    /* PTE for PA base 0x00000000 */
    sd   t4, 0(t0)        /* entry[0] = identity map for 0x00000000-0x3FFFFFFF */

    ret

/*
 * enable_mmu - enable Sv39 paging
 *
 * Writes satp = SV39 mode | PPN of root page table.
 * MUST be called while executing at a physical address that is identity-mapped.
 * Clobbers: t0, t1
 */
.globl enable_mmu
.type enable_mmu, @function
enable_mmu:
    /* Fence to ensure page table writes are visible */
    sfence.vma zero, zero

    /* satp = SV39 mode | (root_pa >> 12) */
    lla  t0, _boot_pt_l1
    srli t0, t0, 12
    li   t1, SATP_SV39_MODE
    or   t0, t0, t1
    csrw satp, t0

    /* Flush TLB */
    sfence.vma zero, zero

    ret

/*
 * remove_identity_mapping - remove the identity map after switching to virtual addresses
 *
 * Zeroes the identity-map PTE so only the virtual mapping remains.
 * Must be called from virtual address space.
 * Uses _page_tables (virtual alias for _boot_pt_l1) since we're in virtual space.
 * Clobbers: t0-t2
 */
.section .vboot.text, "ax"
.globl remove_identity_mapping
.type remove_identity_mapping, @function
remove_identity_mapping:
    la   t0, _page_tables   /* Virtual address of root page table */
    li   t1, CONFIG_PRTOS_LOAD_ADDR
    srli t1, t1, SV39_GIGAPAGE_SHIFT
    andi t1, t1, 0x1FF
    slli t1, t1, 3
    add  t0, t0, t1

    /* Only remove if identity != virtual (they might share the same VPN[2]) */
    li   t1, CONFIG_PRTOS_LOAD_ADDR
    li   t2, CONFIG_PRTOS_OFFSET
    srli t1, t1, SV39_GIGAPAGE_SHIFT
    srli t2, t2, SV39_GIGAPAGE_SHIFT
    andi t1, t1, 0x1FF
    andi t2, t2, 0x1FF
    beq  t1, t2, 1f      /* Skip if same slot */
    sd   zero, 0(t0)     /* Clear identity map PTE */
    sfence.vma zero, zero
1:
    ret

/*
 * Boot-time root page table (must be 4KB-aligned, in .boot.data so it's at physical address)
 */
.section .boot.data, "aw"
.align 12
.globl _boot_pt_l1
_boot_pt_l1:
    .space 4096
