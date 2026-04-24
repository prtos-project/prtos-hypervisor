/*
 * FILE: entry.S
 *
 * LoongArch 64-bit trap entry/exit for PRTOS hypervisor
 * Handles exceptions from both host (PLV0) and guest partitions
 *
 * http://www.prtos.org/
 */

#include <arch/asm_offsets.h>

/* Register save area size: 31 GP regs + ERA + CRMD + PRMD + ESTAT + BADV = 36 * 8 = 288 bytes */
#define REG_SIZE 288

/* Offsets into the register save frame (matching struct cpu_user_regs) */
#define REG_RA    0
#define REG_TP    8
#define REG_SP    16
#define REG_A0    24
#define REG_A1    32
#define REG_A2    40
#define REG_A3    48
#define REG_A4    56
#define REG_A5    64
#define REG_A6    72
#define REG_A7    80
#define REG_T0    88
#define REG_T1    96
#define REG_T2    104
#define REG_T3    112
#define REG_T4    120
#define REG_T5    128
#define REG_T6    136
#define REG_T7    144
#define REG_T8    152
#define REG_U0    160
#define REG_FP    168
#define REG_S0    176
#define REG_S1    184
#define REG_S2    192
#define REG_S3    200
#define REG_S4    208
#define REG_S5    216
#define REG_S6    224
#define REG_S7    232
#define REG_S8    240
#define REG_ERA   248
#define REG_CRMD  256
#define REG_PRMD  264
#define REG_ESTAT 272
#define REG_BADV  280

/* CSR numbers */
#define CSR_CRMD   0x0
#define CSR_PRMD   0x1
#define CSR_ESTAT  0x5
#define CSR_ERA    0x6
#define CSR_BADV   0x7
#define CSR_SAVE0  0x30
#define CSR_SAVE1  0x31
#define CSR_SAVE2  0x32
#define CSR_SAVE5  0x35
#define CSR_GSTAT  0x50
#define CSR_GTLBC  0x15

.section .text
.align 12

/*
 * _trap_entry - Exception entry point
 *
 * LoongArch uses a single exception entry (CSR.EENTRY).
 * On entry, the CPU is at PLV0 (host privilege level).
 *
 * SAVE0 = host sp (saved when entering guest via JMP_PARTITION)
 * SAVE1 = cpu_id
 * SAVE2 = temporary scratch (used here to save t0)
 *
 * We save all GP regs, CSRs, then call the C trap handler.
 */
.globl _trap_entry
.type _trap_entry, @function
_trap_entry:
    /* Save t0 to SAVE2 (scratch), preserving SAVE0 (host sp) */
    csrwr   $t0, CSR_SAVE2      /* Save t0 temporarily in SAVE2 */

    /* Check LVZ guest exit first: GSTAT.PVM (bit 1) */
    csrrd   $t0, CSR_GSTAT       /* 0x50 */
    andi    $t0, $t0, 0x2        /* PVM bit */
    bnez    $t0, _trap_from_lvz_guest

    /* Check PLV3 para-virt guest: PRMD.PPLV */
    csrrd   $t0, CSR_PRMD       /* Check PRMD.PPLV for privilege level */
    andi    $t0, $t0, 0x3       /* PPLV field (bits 1:0) */
    bnez    $t0, _trap_from_guest

    /* Trap from host (PLV0): use current sp */
    csrrd   $t0, CSR_SAVE2      /* Restore t0 from SAVE2 */
    b       _trap_save_regs

_trap_from_lvz_guest:
    /* LVZ guest exit: clear PVM, clear GTLBC.TGID, set LVZ flag */
    /* Clear GSTAT.PVM */
    li.w    $t0, 0x2
    csrxchg $zero, $t0, CSR_GSTAT

    /* Clear GTLBC.TGID (set to 0 for root TLB ops) */
    csrrd   $t0, CSR_GTLBC
    bstrins.w $t0, $zero, 23, 16
    csrwr   $t0, CSR_GTLBC

    /* Set LVZ flag in SAVE5 */
    li.w    $t0, 1
    csrwr   $t0, CSR_SAVE5

    /* Swap to host stack from SAVE0 */
    move    $t0, $sp             /* t0 = guest sp */
    csrrd   $sp, CSR_SAVE0       /* sp = host sp */
    b       _trap_save_regs_guest

_trap_from_guest:
    /* Trap from guest: swap to host stack from SAVE0.
     * SAVE0 has the host sp (set by JMP_PARTITION).
     * SAVE2 has the guest's original t0.
     * DMW PLV3 stays enabled (set at boot) — host code at PLV0
     * is unaffected since DMW PLV0 is always enabled. */
    move    $t0, $sp             /* t0 = guest sp (to be saved in frame) */
    csrrd   $sp, CSR_SAVE0       /* sp = host sp */
    b       _trap_save_regs_guest

_trap_save_regs:
    /* Save registers on current (host) stack */
    addi.d  $sp, $sp, -REG_SIZE

    st.d    $ra,  $sp, REG_RA
    /* sp saved below */
    st.d    $tp,  $sp, REG_TP
    st.d    $a0,  $sp, REG_A0
    st.d    $a1,  $sp, REG_A1
    st.d    $a2,  $sp, REG_A2
    st.d    $a3,  $sp, REG_A3
    st.d    $a4,  $sp, REG_A4
    st.d    $a5,  $sp, REG_A5
    st.d    $a6,  $sp, REG_A6
    st.d    $a7,  $sp, REG_A7
    st.d    $t0,  $sp, REG_T0
    st.d    $t1,  $sp, REG_T1
    st.d    $t2,  $sp, REG_T2
    st.d    $t3,  $sp, REG_T3
    st.d    $t4,  $sp, REG_T4
    st.d    $t5,  $sp, REG_T5
    st.d    $t6,  $sp, REG_T6
    st.d    $t7,  $sp, REG_T7
    st.d    $t8,  $sp, REG_T8
    st.d    $r21, $sp, REG_U0
    st.d    $fp,  $sp, REG_FP
    st.d    $s0,  $sp, REG_S0
    st.d    $s1,  $sp, REG_S1
    st.d    $s2,  $sp, REG_S2
    st.d    $s3,  $sp, REG_S3
    st.d    $s4,  $sp, REG_S4
    st.d    $s5,  $sp, REG_S5
    st.d    $s6,  $sp, REG_S6
    st.d    $s7,  $sp, REG_S7
    st.d    $s8,  $sp, REG_S8

    /* Save original sp (host sp before addi) */
    addi.d  $t0, $sp, REG_SIZE
    st.d    $t0, $sp, REG_SP

    b       _trap_save_csrs

_trap_save_regs_guest:
    /* Save registers for guest trap. t0 = guest sp */
    addi.d  $sp, $sp, -REG_SIZE

    st.d    $ra,  $sp, REG_RA
    st.d    $t0,  $sp, REG_SP   /* guest sp (saved in t0) */
    st.d    $tp,  $sp, REG_TP
    st.d    $a0,  $sp, REG_A0
    st.d    $a1,  $sp, REG_A1
    st.d    $a2,  $sp, REG_A2
    st.d    $a3,  $sp, REG_A3
    st.d    $a4,  $sp, REG_A4
    st.d    $a5,  $sp, REG_A5
    st.d    $a6,  $sp, REG_A6
    st.d    $a7,  $sp, REG_A7
    /* Recover guest t0 from SAVE2 */
    csrrd   $t0,  CSR_SAVE2
    st.d    $t0,  $sp, REG_T0
    st.d    $t1,  $sp, REG_T1
    st.d    $t2,  $sp, REG_T2
    st.d    $t3,  $sp, REG_T3
    st.d    $t4,  $sp, REG_T4
    st.d    $t5,  $sp, REG_T5
    st.d    $t6,  $sp, REG_T6
    st.d    $t7,  $sp, REG_T7
    st.d    $t8,  $sp, REG_T8
    st.d    $r21, $sp, REG_U0
    st.d    $fp,  $sp, REG_FP
    st.d    $s0,  $sp, REG_S0
    st.d    $s1,  $sp, REG_S1
    st.d    $s2,  $sp, REG_S2
    st.d    $s3,  $sp, REG_S3
    st.d    $s4,  $sp, REG_S4
    st.d    $s5,  $sp, REG_S5
    st.d    $s6,  $sp, REG_S6
    st.d    $s7,  $sp, REG_S7
    st.d    $s8,  $sp, REG_S8

_trap_save_csrs:
    /* Save CSRs */
    csrrd   $t0, CSR_ERA
    st.d    $t0, $sp, REG_ERA
    csrrd   $t0, CSR_CRMD
    st.d    $t0, $sp, REG_CRMD
    csrrd   $t0, CSR_PRMD
    st.d    $t0, $sp, REG_PRMD
    csrrd   $t0, CSR_ESTAT
    st.d    $t0, $sp, REG_ESTAT
    csrrd   $t0, CSR_BADV
    st.d    $t0, $sp, REG_BADV

    /* Call C trap handler: loongarch64_trap_handler(regs) */
    move    $a0, $sp
    bl      loongarch64_trap_handler

    /* Restore CSRs */
    ld.d    $t0, $sp, REG_ERA
    csrwr   $t0, CSR_ERA
    ld.d    $t0, $sp, REG_PRMD
    csrwr   $t0, CSR_PRMD

    /* Check if returning to guest (PRMD.PPLV != 0)
     * NOTE: csrwr is a SWAP — $t0 now holds the OLD CSR value, not the
     * trap-frame value.  Re-load from the frame for the check. */
    ld.d    $t0, $sp, REG_PRMD
    andi    $t0, $t0, 0x3
    bnez    $t0, _trap_restore_guest

    /* Returning to host: restore and ertn */
_trap_restore_host:
    ld.d    $ra,  $sp, REG_RA
    ld.d    $tp,  $sp, REG_TP
    ld.d    $a0,  $sp, REG_A0
    ld.d    $a1,  $sp, REG_A1
    ld.d    $a2,  $sp, REG_A2
    ld.d    $a3,  $sp, REG_A3
    ld.d    $a4,  $sp, REG_A4
    ld.d    $a5,  $sp, REG_A5
    ld.d    $a6,  $sp, REG_A6
    ld.d    $a7,  $sp, REG_A7
    ld.d    $t0,  $sp, REG_T0
    ld.d    $t1,  $sp, REG_T1
    ld.d    $t2,  $sp, REG_T2
    ld.d    $t3,  $sp, REG_T3
    ld.d    $t4,  $sp, REG_T4
    ld.d    $t5,  $sp, REG_T5
    ld.d    $t6,  $sp, REG_T6
    ld.d    $t7,  $sp, REG_T7
    ld.d    $t8,  $sp, REG_T8
    ld.d    $r21, $sp, REG_U0
    ld.d    $fp,  $sp, REG_FP
    ld.d    $s0,  $sp, REG_S0
    ld.d    $s1,  $sp, REG_S1
    ld.d    $s2,  $sp, REG_S2
    ld.d    $s3,  $sp, REG_S3
    ld.d    $s4,  $sp, REG_S4
    ld.d    $s5,  $sp, REG_S5
    ld.d    $s6,  $sp, REG_S6
    ld.d    $s7,  $sp, REG_S7
    ld.d    $s8,  $sp, REG_S8

    ld.d    $sp,  $sp, REG_SP

    ertn

_trap_restore_guest:
    /* Check if LVZ return (SAVE5 flag) */
    csrrd   $t0, CSR_SAVE5
    beqz    $t0, _plv3_guest_return

    /* LVZ guest re-entry path */
    csrwr   $zero, CSR_SAVE5     /* Clear LVZ flag */

    /* Override ERA from regs frame */
    ld.d    $t0, $sp, REG_ERA
    csrwr   $t0, CSR_ERA

    /* Set PRMD = PIE (for root interrupt enable during ertn) */
    li.w    $t0, 0x4             /* PRMD: PPLV=0, PIE=1 */
    csrwr   $t0, CSR_PRMD

    /* Set GTLBC.TGID = GSTAT.GID */
    csrrd   $t1, CSR_GSTAT
    srli.w  $t1, $t1, 16
    andi    $t1, $t1, 0xFF       /* GID value */
    csrrd   $t0, CSR_GTLBC
    bstrins.w $t0, $t1, 23, 16   /* Insert TGID */
    csrwr   $t0, CSR_GTLBC

    /* Set GSTAT.PVM = 1 (ertn will enter guest mode) */
    li.w    $t0, 0x2
    csrxchg $t0, $t0, CSR_GSTAT

    /* Save host sp to SAVE0 for next trap entry */
    addi.d  $t0, $sp, REG_SIZE
    csrwr   $t0, CSR_SAVE0

    /* Restore all guest GPRs */
    ld.d    $ra,  $sp, REG_RA
    ld.d    $tp,  $sp, REG_TP
    ld.d    $a0,  $sp, REG_A0
    ld.d    $a1,  $sp, REG_A1
    ld.d    $a2,  $sp, REG_A2
    ld.d    $a3,  $sp, REG_A3
    ld.d    $a4,  $sp, REG_A4
    ld.d    $a5,  $sp, REG_A5
    ld.d    $a6,  $sp, REG_A6
    ld.d    $a7,  $sp, REG_A7
    ld.d    $t0,  $sp, REG_T0
    ld.d    $t1,  $sp, REG_T1
    ld.d    $t2,  $sp, REG_T2
    ld.d    $t3,  $sp, REG_T3
    ld.d    $t4,  $sp, REG_T4
    ld.d    $t5,  $sp, REG_T5
    ld.d    $t6,  $sp, REG_T6
    ld.d    $t7,  $sp, REG_T7
    ld.d    $t8,  $sp, REG_T8
    ld.d    $r21, $sp, REG_U0
    ld.d    $fp,  $sp, REG_FP
    ld.d    $s0,  $sp, REG_S0
    ld.d    $s1,  $sp, REG_S1
    ld.d    $s2,  $sp, REG_S2
    ld.d    $s3,  $sp, REG_S3
    ld.d    $s4,  $sp, REG_S4
    ld.d    $s5,  $sp, REG_S5
    ld.d    $s6,  $sp, REG_S6
    ld.d    $s7,  $sp, REG_S7
    ld.d    $s8,  $sp, REG_S8

    ld.d    $sp,  $sp, REG_SP

    ertn

_plv3_guest_return:
    /* PLV3 para-virt guest return: save host sp to SAVE0 for next trap entry.
     * DMW PLV3 stays enabled (set at boot). */
    addi.d  $t0, $sp, REG_SIZE
    csrwr   $t0, CSR_SAVE0

    ld.d    $ra,  $sp, REG_RA
    ld.d    $tp,  $sp, REG_TP
    ld.d    $a0,  $sp, REG_A0
    ld.d    $a1,  $sp, REG_A1
    ld.d    $a2,  $sp, REG_A2
    ld.d    $a3,  $sp, REG_A3
    ld.d    $a4,  $sp, REG_A4
    ld.d    $a5,  $sp, REG_A5
    ld.d    $a6,  $sp, REG_A6
    ld.d    $a7,  $sp, REG_A7
    ld.d    $t0,  $sp, REG_T0
    ld.d    $t1,  $sp, REG_T1
    ld.d    $t2,  $sp, REG_T2
    ld.d    $t3,  $sp, REG_T3
    ld.d    $t4,  $sp, REG_T4
    ld.d    $t5,  $sp, REG_T5
    ld.d    $t6,  $sp, REG_T6
    ld.d    $t7,  $sp, REG_T7
    ld.d    $t8,  $sp, REG_T8
    ld.d    $r21, $sp, REG_U0
    ld.d    $fp,  $sp, REG_FP
    ld.d    $s0,  $sp, REG_S0
    ld.d    $s1,  $sp, REG_S1
    ld.d    $s2,  $sp, REG_S2
    ld.d    $s3,  $sp, REG_S3
    ld.d    $s4,  $sp, REG_S4
    ld.d    $s5,  $sp, REG_S5
    ld.d    $s6,  $sp, REG_S6
    ld.d    $s7,  $sp, REG_S7
    ld.d    $s8,  $sp, REG_S8

    ld.d    $sp,  $sp, REG_SP

    ertn

/*
 * _tlb_refill_entry - TLB refill exception handler
 *
 * Entered via CSR.TLBRENTRY (0x88) when a TLB miss occurs.
 * On entry: DA mode is active (VA = PA), PLV0.
 * CSR.TLBRBADV (0x89) = faulting virtual address.
 *
 * Uses per-CPU tlb_refill_table[] (populated by setup_stage2_mmu in mmu.c)
 * to look up the correct TLBELO flags for each 1MB page.  Pages not in
 * the current partition's configured memory areas have table entry 0 (V=0),
 * causing a Page Invalid exception on access.  This enforces per-partition
 * memory isolation at 1MB granularity.
 */

/* TLB page size 1MB = PS 20 */
#define TLB_PS_1M    20

.align 12
.globl _tlb_refill_entry
.type _tlb_refill_entry, @function
_tlb_refill_entry:
    csrwr   $t0, CSR_SAVE2      /* Save t0 in SAVE2 */
    csrwr   $t1, 0x33           /* Save t1 in SAVE3 */

    csrrd   $t0, 0x89           /* CSR_TLBRBADV */

    /* Check for DMW addresses: top 16 bits indicate DMW region */
    srli.d  $t1, $t0, 48       /* t1 = top 16 bits of BADV */

    /* DMW1: 0x9000 (cached kernel memory - most common) */
    li.w    $t0, 0x9000
    beq     $t1, $t0, _refill_dmw

    /* DMW0: 0x8000 (uncacheable/IO) */
    li.w    $t0, 0x8000
    beq     $t1, $t0, _refill_dmw

    /* DMW2: 0xa000 (weak uncacheable) */
    li.w    $t0, 0xa000
    beq     $t1, $t0, _refill_dmw

    /* Not DMW: check if direct GPA < 4GB */
    csrrd   $t0, 0x89           /* BADV */
    srli.d  $t0, $t0, 32       /* if >= 4GB, result is nonzero */
    bnez    $t0, _refill_deny

    /* Direct GPA path: look up in refill table */
    csrrd   $t0, 0x34           /* CSR_SAVE4 = table PA */
    beqz    $t0, _refill_deny

    /* Look up TLBELO0: even page of the 2MB pair */
    csrrd   $t1, 0x89           /* BADV */
    srli.d  $t1, $t1, 20       /* 1MB page index */
    bstrins.d $t1, $zero, 0, 0 /* Clear bit 0 → even page index */
    slli.d  $t1, $t1, 3        /* * 8 bytes per entry */
    add.d   $t1, $t0, $t1
    ld.d    $t1, $t1, 0        /* t1 = TLBELO0 value (PA|flags or 0) */
    csrwr   $t1, 0x8C          /* CSR_TLBRELO0 */

    /* Look up TLBELO1: odd page of the 2MB pair */
    csrrd   $t1, 0x89           /* BADV */
    srli.d  $t1, $t1, 20       /* 1MB page index */
    ori     $t1, $t1, 1        /* Set bit 0 → odd page index */
    slli.d  $t1, $t1, 3        /* * 8 bytes per entry */
    add.d   $t1, $t0, $t1
    ld.d    $t1, $t1, 0        /* t1 = TLBELO1 value (PA|flags or 0) */
    csrwr   $t1, 0x8D          /* CSR_TLBRELO1 */

    /* Set TLBREHI: 2MB-aligned pair base | page size */
    csrrd   $t0, 0x89           /* BADV */
    bstrins.d $t0, $zero, 20, 0 /* 2MB aligned base */
    ori     $t0, $t0, TLB_PS_1M
    csrwr   $t0, 0x8E          /* CSR_TLBREHI */

    li.w    $t0, (TLB_PS_1M << 24)
    csrwr   $t0, 0x10          /* CSR_TLBIDX */
    tlbfill
    b       _refill_done

_refill_dmw:
    /*
     * DMW address: extract GPA from low 48 bits of BADV.
     * PA = BADV & 0x0000_FFFF_FFFF_FFFF
     * Look up GPA in refill table, map DMW-VA → HPA.
     */
    csrrd   $t0, 0x34           /* CSR_SAVE4 = table PA */
    beqz    $t0, _refill_deny

    /* Extract GPA and check < 4GB */
    csrrd   $t1, 0x89           /* BADV */
    /* Mask to get GPA: clear top 16 bits */
    bstrins.d $t1, $zero, 63, 48  /* Clear bits [63:48] → GPA */
    srli.d  $t1, $t1, 32       /* Check if GPA >= 4GB */
    bnez    $t1, _refill_deny

    /* Look up even page: GPA 1MB page index, even */
    csrrd   $t1, 0x89           /* BADV */
    bstrins.d $t1, $zero, 63, 48  /* GPA */
    srli.d  $t1, $t1, 20       /* 1MB index */
    bstrins.d $t1, $zero, 0, 0 /* Even */
    slli.d  $t1, $t1, 3        /* * 8 */
    add.d   $t1, $t0, $t1
    ld.d    $t1, $t1, 0        /* TLBELO0 (HPA | flags) */
    csrwr   $t1, 0x8C          /* CSR_TLBRELO0 */

    /* Look up odd page */
    csrrd   $t1, 0x89           /* BADV */
    bstrins.d $t1, $zero, 63, 48  /* GPA */
    srli.d  $t1, $t1, 20       /* 1MB index */
    ori     $t1, $t1, 1        /* Odd */
    slli.d  $t1, $t1, 3        /* * 8 */
    add.d   $t1, $t0, $t1
    ld.d    $t1, $t1, 0        /* TLBELO1 */
    csrwr   $t1, 0x8D          /* CSR_TLBRELO1 */

    /* TLBREHI: use FULL DMW virtual address (2MB aligned) | PS */
    csrrd   $t0, 0x89           /* BADV (DMW address) */
    bstrins.d $t0, $zero, 20, 0 /* 2MB aligned */
    ori     $t0, $t0, TLB_PS_1M
    csrwr   $t0, 0x8E          /* CSR_TLBREHI */

    li.w    $t0, (TLB_PS_1M << 24)
    csrwr   $t0, 0x10          /* CSR_TLBIDX */
    tlbfill
    b       _refill_done

_refill_deny:
    /* Unknown address: fill with V=0, G=1 (invalid, global) entries → causes PIL/PIS/PIF.
     * G=1 ensures guest TLBSRCH can find and overwrite these entries later.
     * Use 16KB page size (PS=14) matching guest kernel's STLBPS to avoid
     * multi-hit with valid entries filled by the guest. */
    csrrd   $t0, 0x89           /* BADV */
    bstrins.d $t0, $zero, 14, 0 /* Clear low 15 bits: 32KB pair alignment for PS=14 */
    ori     $t0, $t0, 14        /* PS = 14 (16KB) */
    csrwr   $t0, 0x8E          /* CSR_TLBREHI */
    li.w    $t0, 0x40           /* G=1 (bit 6), V=0 */
    csrwr   $t0, 0x8C          /* TLBRELO0 = 0x40 (V=0, G=1) */
    csrwr   $t0, 0x8D          /* TLBRELO1 = 0x40 (V=0, G=1) */
    li.w    $t0, (14 << 24)     /* TLBIDX PS=14 */
    csrwr   $t0, 0x10          /* CSR_TLBIDX */
    tlbfill

_refill_done:
    /* Restore scratch registers */
    csrrd   $t1, 0x33           /* Restore t1 from SAVE3 */
    csrrd   $t0, CSR_SAVE2      /* Restore t0 from SAVE2 */

    /* Return from TLB refill */
    ertn

/*
 * leave_hypervisor_to_guest - Enter guest partition
 *
 * a0 = pointer to cpu_user_regs (saved guest state)
 */
.globl leave_hypervisor_to_guest
.type leave_hypervisor_to_guest, @function
.align 4
leave_hypervisor_to_guest:
    move    $sp, $a0

    /* Restore ERA and PRMD for guest */
    ld.d    $t0, $sp, REG_ERA
    csrwr   $t0, CSR_ERA
    ld.d    $t0, $sp, REG_PRMD
    csrwr   $t0, CSR_PRMD

    /* Restore all GP regs */
    ld.d    $ra,  $sp, REG_RA
    ld.d    $tp,  $sp, REG_TP
    ld.d    $a0,  $sp, REG_A0
    ld.d    $a1,  $sp, REG_A1
    ld.d    $a2,  $sp, REG_A2
    ld.d    $a3,  $sp, REG_A3
    ld.d    $a4,  $sp, REG_A4
    ld.d    $a5,  $sp, REG_A5
    ld.d    $a6,  $sp, REG_A6
    ld.d    $a7,  $sp, REG_A7
    ld.d    $t0,  $sp, REG_T0
    ld.d    $t1,  $sp, REG_T1
    ld.d    $t2,  $sp, REG_T2
    ld.d    $t3,  $sp, REG_T3
    ld.d    $t4,  $sp, REG_T4
    ld.d    $t5,  $sp, REG_T5
    ld.d    $t6,  $sp, REG_T6
    ld.d    $t7,  $sp, REG_T7
    ld.d    $t8,  $sp, REG_T8
    ld.d    $r21, $sp, REG_U0
    ld.d    $fp,  $sp, REG_FP
    ld.d    $s0,  $sp, REG_S0
    ld.d    $s1,  $sp, REG_S1
    ld.d    $s2,  $sp, REG_S2
    ld.d    $s3,  $sp, REG_S3
    ld.d    $s4,  $sp, REG_S4
    ld.d    $s5,  $sp, REG_S5
    ld.d    $s6,  $sp, REG_S6
    ld.d    $s7,  $sp, REG_S7
    ld.d    $s8,  $sp, REG_S8

    ld.d    $sp,  $sp, REG_SP

    ertn
