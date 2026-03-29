/*
 * FILE: entry.S
 *
 * RISC-V 64-bit trap entry/exit for PRTOS hypervisor
 * Handles traps from both HS-mode (hypervisor) and VS-mode (guest)
 *
 * www.prtos.org
 */

#include <arch/asm_offsets.h>

/* Register save area size: 32 GP regs + sepc + sstatus + scause + stval + hstatus = 37 * 8 + 8 pad = 304 bytes */
#define REG_SIZE 304

/* Offsets into the register save frame */
#define REG_RA    0
#define REG_SP    8
#define REG_GP    16
#define REG_TP    24
#define REG_T0    32
#define REG_T1    40
#define REG_T2    48
#define REG_S0    56
#define REG_S1    64
#define REG_A0    72
#define REG_A1    80
#define REG_A2    88
#define REG_A3    96
#define REG_A4    104
#define REG_A5    112
#define REG_A6    120
#define REG_A7    128
#define REG_S2    136
#define REG_S3    144
#define REG_S4    152
#define REG_S5    160
#define REG_S6    168
#define REG_S7    176
#define REG_S8    184
#define REG_S9    192
#define REG_S10   200
#define REG_S11   208
#define REG_T3    216
#define REG_T4    224
#define REG_T5    232
#define REG_T6    240
#define REG_SEPC  248
#define REG_SSTATUS 256
#define REG_SCAUSE 264
#define REG_STVAL 272
#define REG_HSTATUS 280

.section .text
.align 2

/*
 * _trap_entry - HS-mode trap entry point
 *
 * On entry, we may have trapped from either HS-mode (hypervisor code)
 * or VS-mode (guest partition code). We save all GP regs on the
 * current stack, then call the C trap handler.
 */
.globl _trap_entry
.type _trap_entry, @function
_trap_entry:
    /* Check if we came from VS-mode (guest) or HS-mode (hypervisor).
     * sscratch is 0 when in HS-mode, non-zero (= kernel sp) when in VS-mode. */
    csrrw sp, sscratch, sp
    bnez sp, _trap_from_guest

    /* Trap from HS-mode: sscratch was 0, sp now 0, old sp in sscratch */
    csrr sp, sscratch       /* Restore original HS-mode sp */
    j _trap_save_regs

_trap_from_guest:
    /* Trap from VS-mode: sp = host stack with host_tp slot at top (from sscratch).
     * sscratch = guest sp.  Skip past the 8-byte host_tp slot so that
     * _trap_save_regs sees the original host sp. Host tp will be restored
     * after all guest registers (including guest tp) are saved. */
    addi sp, sp, 8
    /* Fall through to save regs */

_trap_save_regs:
    /* Save registers on the current (hypervisor) stack */
    addi sp, sp, -REG_SIZE

    sd ra,   REG_RA(sp)
    /* sp saved below (guest or hypervisor sp) */
    sd gp,   REG_GP(sp)
    sd tp,   REG_TP(sp)
    sd t0,   REG_T0(sp)
    sd t1,   REG_T1(sp)
    sd t2,   REG_T2(sp)
    sd s0,   REG_S0(sp)
    sd s1,   REG_S1(sp)
    sd a0,   REG_A0(sp)
    sd a1,   REG_A1(sp)
    sd a2,   REG_A2(sp)
    sd a3,   REG_A3(sp)
    sd a4,   REG_A4(sp)
    sd a5,   REG_A5(sp)
    sd a6,   REG_A6(sp)
    sd a7,   REG_A7(sp)
    sd s2,   REG_S2(sp)
    sd s3,   REG_S3(sp)
    sd s4,   REG_S4(sp)
    sd s5,   REG_S5(sp)
    sd s6,   REG_S6(sp)
    sd s7,   REG_S7(sp)
    sd s8,   REG_S8(sp)
    sd s9,   REG_S9(sp)
    sd s10,  REG_S10(sp)
    sd s11,  REG_S11(sp)
    sd t3,   REG_T3(sp)
    sd t4,   REG_T4(sp)
    sd t5,   REG_T5(sp)
    sd t6,   REG_T6(sp)

    /* Save the original sp (the one before trap entry - either guest or hypervisor sp) */
    csrr t0, sscratch       /* This holds the sp from before the swap */
    sd t0,   REG_SP(sp)

    /* Clear sscratch (we're now in HS-mode context) */
    csrw sscratch, zero

    /* Save CSRs */
    csrr t0, sepc
    sd t0,   REG_SEPC(sp)
    csrr t0, sstatus
    sd t0,   REG_SSTATUS(sp)
    csrr t0, scause
    sd t0,   REG_SCAUSE(sp)
    csrr t0, stval
    sd t0,   REG_STVAL(sp)
    csrr t0, hstatus
    sd t0,   REG_HSTATUS(sp)

    /* If trapped from VS-mode (hstatus.SPV=1), restore host tp from the
     * slot saved at (host_sp - 8) = sp + REG_SIZE - 8.  For HS-mode traps
     * tp is already the host's value, so skip this. */
    andi t1, t0, (1 << 7)
    beqz t1, 1f
    ld tp, (REG_SIZE - 8)(sp)
1:

    /* Call C trap handler: riscv64_trap_handler(regs) */
    mv a0, sp
    call riscv64_trap_handler

    /* Restore CSRs */
    ld t0,   REG_SEPC(sp)
    csrw sepc, t0
    ld t0,   REG_SSTATUS(sp)
    csrw sstatus, t0
    ld t0,   REG_HSTATUS(sp)
    csrw hstatus, t0

    /* Check if we're returning to VS-mode: check hstatus.SPV (bit 7) */
    andi t0, t0, (1 << 7)
    beqz t0, _trap_restore_hs

    /* Returning to VS-mode: save host tp to the stack slot and set
     * sscratch so that the next trap entry finds host_tp at (sscratch). */
    addi t0, sp, REG_SIZE       /* t0 = host_sp (above register frame) */
    sd tp, -8(t0)               /* Save host tp at host_sp - 8 */
    addi t0, t0, -8             /* t0 = host_sp - 8 (slot address) */
    csrw sscratch, t0
    j _trap_restore_regs

_trap_restore_hs:
    /* Returning to HS-mode: sscratch should be 0 */
    csrw sscratch, zero

_trap_restore_regs:
    /* Restore GP registers */
    ld ra,   REG_RA(sp)
    /* sp restored last */
    ld gp,   REG_GP(sp)
    ld tp,   REG_TP(sp)
    ld t0,   REG_T0(sp)
    ld t1,   REG_T1(sp)
    ld t2,   REG_T2(sp)
    ld s0,   REG_S0(sp)
    ld s1,   REG_S1(sp)
    ld a0,   REG_A0(sp)
    ld a1,   REG_A1(sp)
    ld a2,   REG_A2(sp)
    ld a3,   REG_A3(sp)
    ld a4,   REG_A4(sp)
    ld a5,   REG_A5(sp)
    ld a6,   REG_A6(sp)
    ld a7,   REG_A7(sp)
    ld s2,   REG_S2(sp)
    ld s3,   REG_S3(sp)
    ld s4,   REG_S4(sp)
    ld s5,   REG_S5(sp)
    ld s6,   REG_S6(sp)
    ld s7,   REG_S7(sp)
    ld s8,   REG_S8(sp)
    ld s9,   REG_S9(sp)
    ld s10,  REG_S10(sp)
    ld s11,  REG_S11(sp)
    ld t3,   REG_T3(sp)
    ld t4,   REG_T4(sp)
    ld t5,   REG_T5(sp)
    ld t6,   REG_T6(sp)

    /* Restore sp (either guest sp or hypervisor sp) */
    ld sp,   REG_SP(sp)

    sret

/*
 * leave_hypervisor_to_guest - Enter VS-mode guest partition
 *
 * a0 = pointer to cpu_user_regs (saved guest state)
 *
 * Restores all GP regs + sepc/sstatus from the frame and does sret
 * to jump into guest VS-mode.
 */
.globl leave_hypervisor_to_guest
.type leave_hypervisor_to_guest, @function
.align 2
leave_hypervisor_to_guest:
    mv sp, a0

    /* Restore sepc and sstatus for guest */
    ld t0, REG_SEPC(sp)
    csrw sepc, t0
    ld t0, REG_SSTATUS(sp)
    csrw sstatus, t0

    /* Restore all GP regs */
    ld ra,   REG_RA(sp)
    ld gp,   REG_GP(sp)
    ld tp,   REG_TP(sp)
    ld t0,   REG_T0(sp)
    ld t1,   REG_T1(sp)
    ld t2,   REG_T2(sp)
    ld s0,   REG_S0(sp)
    ld s1,   REG_S1(sp)
    ld a0,   REG_A0(sp)
    ld a1,   REG_A1(sp)
    ld a2,   REG_A2(sp)
    ld a3,   REG_A3(sp)
    ld a4,   REG_A4(sp)
    ld a5,   REG_A5(sp)
    ld a6,   REG_A6(sp)
    ld a7,   REG_A7(sp)
    ld s2,   REG_S2(sp)
    ld s3,   REG_S3(sp)
    ld s4,   REG_S4(sp)
    ld s5,   REG_S5(sp)
    ld s6,   REG_S6(sp)
    ld s7,   REG_S7(sp)
    ld s8,   REG_S8(sp)
    ld s9,   REG_S9(sp)
    ld s10,  REG_S10(sp)
    ld s11,  REG_S11(sp)
    ld t3,   REG_T3(sp)
    ld t4,   REG_T4(sp)
    ld t5,   REG_T5(sp)
    ld t6,   REG_T6(sp)

    ld sp,   REG_SP(sp)

    sret
