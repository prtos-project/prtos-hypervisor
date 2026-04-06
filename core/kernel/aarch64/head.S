/*
 * FILE: head.S
 *
 * AArch64 early page table setup and MMU enable
 * Creates initial mapping for EL2 (TTBR0_EL2, 4KB granule, 48-bit VA).
 *
 * 4-level page tables:
 *   L0: 512 entries, VA[47:39], 512GB per entry
 *   L1: 512 entries, VA[38:30], 1GB per entry (block descriptor)
 *   L2: 512 entries, VA[29:21], 2MB per entry
 *   L3: 512 entries, VA[20:12], 4KB per entry
 *
 * Boot-time mapping uses L0→L1 with 1GB block descriptors at L1:
 *   L0[0]  → _boot_pt_l1_id    (identity + MMIO mapping)
 *   L0[20] → _boot_pt_l1_kern  (kernel virtual mapping)
 *
 * After the kernel transitions to virtual addresses, the identity L1 entry
 * is removed (MMIO entry at L1[0] is preserved in L0[0]).
 *
 * http://www.prtos.org/
 */

#include <linkage.h>
#include <arch/paging.h>
#include <arch/prtos_def.h>

/* AArch64 block descriptor flags for 1GB blocks at level 1 */
#define BLOCK_DESC_VALID  (1 << 0)
#define BLOCK_DESC_BLOCK  (0 << 1)
#define BLOCK_DESC_TABLE  (1 << 1)
#define BLOCK_ATTR_IDX0   (0 << 2)  /* AttrIndx=0 (Normal memory from MAIR) */
#define BLOCK_ATTR_AF     (1 << 10) /* Access flag */
#define BLOCK_ATTR_ISH    (3 << 8)  /* Inner Shareable */
#define BLOCK_ATTR_AP_RW  (0 << 7)  /* AP[2:1]=00: EL2 RW */
#define BLOCK_ATTR_UXN    (1ULL << 54)

/* Normal cacheable block: Valid + Block + AttrIdx=0 + AF + ISH */
#define BLOCK_FLAGS (BLOCK_DESC_VALID | BLOCK_DESC_BLOCK | \
                     BLOCK_ATTR_IDX0 | BLOCK_ATTR_AF | BLOCK_ATTR_ISH)

/* Device memory block: AttrIdx=1 */
#define BLOCK_ATTR_IDX1   (1 << 2)
#define DEVICE_BLOCK_FLAGS (BLOCK_DESC_VALID | BLOCK_DESC_BLOCK | \
                            BLOCK_ATTR_IDX1 | BLOCK_ATTR_AF | BLOCK_ATTR_ISH)

/* Table descriptor: points to next-level table */
#define TABLE_FLAGS (BLOCK_DESC_VALID | BLOCK_DESC_TABLE)

#define L0_BLOCK_SHIFT 39    /* 512GB per L0 entry */
#define L1_BLOCK_SHIFT 30    /* 1GB per level-1 entry */

.section .boot.text, "ax"

/*
 * setup_boot_page_table - create initial 4-level page tables for boot
 *
 * Uses _boot_pt_l0 as the L0 root table.
 * L0[0] → _boot_pt_l1_id (identity map + MMIO)
 * L0[20] → _boot_pt_l1_kern (kernel virtual mapping)
 *
 * _boot_pt_l1_id:
 *   [0] = 1GB device block at PA 0 (UART, GIC)
 *   [PA>>30] = 1GB normal block containing CONFIG_PRTOS_LOAD_ADDR
 *
 * _boot_pt_l1_kern:
 *   [0] = 1GB normal block at PA base containing CONFIG_PRTOS_LOAD_ADDR
 *
 * Clobbers: x0-x8
 */
.globl setup_boot_page_table
.type setup_boot_page_table, @function
setup_boot_page_table:
    /* Zero all three boot tables */
    adrp x0, _boot_pt_l0
    add  x0, x0, :lo12:_boot_pt_l0
    adrp x1, _boot_pt_l1_id
    add  x1, x1, :lo12:_boot_pt_l1_id
    adrp x2, _boot_pt_l1_kern
    add  x2, x2, :lo12:_boot_pt_l1_kern

    /* Zero L0 table */
    mov  x3, x0
    add  x4, x0, #4096
1:  str  xzr, [x3], #8
    cmp  x3, x4
    b.lt 1b

    /* Zero L1 identity table */
    mov  x3, x1
    add  x4, x1, #4096
2:  str  xzr, [x3], #8
    cmp  x3, x4
    b.lt 2b

    /* Zero L1 kernel table */
    mov  x3, x2
    add  x4, x2, #4096
3:  str  xzr, [x3], #8
    cmp  x3, x4
    b.lt 3b

    /* === L0 entries === */

    /* L0[0] → _boot_pt_l1_id (table descriptor) */
    ldr  x5, =TABLE_FLAGS
    orr  x5, x1, x5            /* PA of _boot_pt_l1_id | TABLE_FLAGS */
    str  x5, [x0, #0]          /* L0 index 0 */

    /* L0[20] → _boot_pt_l1_kern (table descriptor) */
    ldr  x5, =TABLE_FLAGS
    orr  x5, x2, x5            /* PA of _boot_pt_l1_kern | TABLE_FLAGS */
    str  x5, [x0, #(20 * 8)]   /* L0 index 20 */

    /* === L1 identity table entries === */

    /* Calculate the 1GB-aligned physical base for LOAD_ADDR */
    ldr  x3, =CONFIG_PRTOS_LOAD_ADDR
    mov  x4, #(1 << L1_BLOCK_SHIFT)
    sub  x5, x4, #1            /* x5 = 0x3FFFFFFF */
    bic  x6, x3, x5            /* x6 = PA base (1GB-aligned) */

    /* Build normal block descriptor: PA_base | BLOCK_FLAGS */
    ldr  x7, =BLOCK_FLAGS
    orr  x7, x6, x7            /* x7 = identity block descriptor */

    /* L1_id[PA>>30] = 1GB normal block (identity map for kernel PA) */
    lsr  x3, x3, #L1_BLOCK_SHIFT
    and  x3, x3, #0x1FF        /* L1 index */
    str  x7, [x1, x3, lsl #3]

    /* L1_id[0] = 1GB device block at PA 0 (MMIO: UART, GIC) */
    ldr  x7, =DEVICE_BLOCK_FLAGS
    str  x7, [x1]

    /* === L1 kernel table entries === */

    /* L1_kern[0] = 1GB normal block at same PA base as identity */
    ldr  x7, =BLOCK_FLAGS
    orr  x7, x6, x7
    str  x7, [x2]              /* L1 index 0 in kernel table */

    ret

/*
 * enable_mmu - enable EL2 MMU with 4KB granule, 48-bit VA (T0SZ=16)
 *
 * Sets up MAIR_EL2, TCR_EL2, TTBR0_EL2, then enables SCTLR_EL2.M
 * Clobbers: x0-x4
 */
.globl enable_mmu
.type enable_mmu, @function
enable_mmu:
    /* Invalidate TLBs */
    tlbi alle2
    dsb  sy
    isb

    /* Set MAIR_EL2:
     * Attr0 = 0xFF (Normal memory, Write-Back, Read-Write Allocate)
     * Attr1 = 0x04 (Device-nGnRE) */
    ldr  x0, =0x00000000000004FF
    msr  mair_el2, x0

    /* Set TCR_EL2:
     * T0SZ  = 16 (48-bit VA → 2^48 = 256TB)
     * IRGN0 = 01 (Normal WB RA WA cacheable)
     * ORGN0 = 01 (Normal WB RA WA cacheable)
     * SH0   = 11 (Inner Shareable)
     * TG0   = 00 (4KB granule)
     * PS    = 010 (40-bit PA) */
    mov  x0, #16               /* T0SZ = 16 for 48-bit VA */
    orr  x0, x0, #(1 << 8)    /* IRGN0 = WB RA WA */
    orr  x0, x0, #(1 << 10)   /* ORGN0 = WB RA WA */
    orr  x0, x0, #(3 << 12)   /* SH0 = Inner Shareable */
    orr  x0, x0, #(2 << 16)   /* PS = 40-bit PA */
    msr  tcr_el2, x0
    isb

    /* Set TTBR0_EL2 = physical address of L0 root table */
    adrp x0, _boot_pt_l0
    add  x0, x0, :lo12:_boot_pt_l0
    msr  ttbr0_el2, x0
    isb

    /* Enable MMU: set SCTLR_EL2.M, .C, .I */
    mrs  x0, sctlr_el2
    orr  x0, x0, #(1 << 0)    /* M: enable MMU */
    orr  x0, x0, #(1 << 2)    /* C: enable data cache */
    orr  x0, x0, #(1 << 12)   /* I: enable instruction cache */
    bic  x0, x0, #(1 << 19)   /* WXN: clear */
    bic  x0, x0, #(1 << 25)   /* EE: clear (little-endian) */
    msr  sctlr_el2, x0
    isb

    ret

/*
 * remove_identity_mapping - remove the identity L1 entry (not the MMIO entry)
 *
 * Clears the identity block in _boot_pt_l1_id that maps the kernel PA range.
 * Preserves L0[0] and L1_id[0] (MMIO device block for UART/GIC).
 * Must be called from virtual address space.
 * Clobbers: x0-x4
 */
.section .vboot.text, "ax"
.globl remove_identity_mapping
.type remove_identity_mapping, @function
remove_identity_mapping:
    /* Get virtual address of L1 identity table via L0 */
    ldr  x0, =_page_tables_l1_id  /* Virtual address of L1 identity table */

    /* Compute L1 index for LOAD_ADDR */
    ldr  x1, =CONFIG_PRTOS_LOAD_ADDR
    lsr  x1, x1, #L1_BLOCK_SHIFT
    and  x1, x1, #0x1FF

    /* Don't clear index 0 (MMIO device block) */
    cbz  x1, 1f
    str  xzr, [x0, x1, lsl #3]  /* Clear identity block entry */
    dsb  sy
    tlbi alle2
    dsb  sy
    isb
1:
    ret

/*
 * Boot-time page tables (must be 4KB-aligned, in .boot.data)
 */
.section .boot.data, "aw"
.align 12
.globl _boot_pt_l0
_boot_pt_l0:
    .space 4096

.align 12
.globl _boot_pt_l1_id
_boot_pt_l1_id:
    .space 4096

.align 12
.globl _boot_pt_l1_kern
_boot_pt_l1_kern:
    .space 4096
