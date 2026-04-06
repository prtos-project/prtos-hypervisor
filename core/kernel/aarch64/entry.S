/*
 * FILE: entry.S
 *
 * AArch64 exception vector table and trap entry/exit for PRTOS hypervisor
 * Handles exceptions from both EL2 (hypervisor) and EL1 (guest)
 *
 * http://www.prtos.org/
 */

#include <arch/asm_offsets.h>

/* Register save area: 31 GP regs + SP_EL1 + ELR_EL2 + SPSR_EL2 + ESR_EL2 + FAR_EL2 + HPFAR_EL2 = 37 * 8 = 296, round to 304 */
#define REG_SIZE 304

/* Offsets into the register save frame (struct cpu_user_regs) */
#define REG_X0     0
#define REG_X1     8
#define REG_X2     16
#define REG_X3     24
#define REG_X4     32
#define REG_X5     40
#define REG_X6     48
#define REG_X7     56
#define REG_X8     64
#define REG_X9     72
#define REG_X10    80
#define REG_X11    88
#define REG_X12    96
#define REG_X13    104
#define REG_X14    112
#define REG_X15    120
#define REG_X16    128
#define REG_X17    136
#define REG_X18    144
#define REG_X19    152
#define REG_X20    160
#define REG_X21    168
#define REG_X22    176
#define REG_X23    184
#define REG_X24    192
#define REG_X25    200
#define REG_X26    208
#define REG_X27    216
#define REG_X28    224
#define REG_X29    232
#define REG_X30    240
#define REG_SP     248
#define REG_ELR    256
#define REG_SPSR   264
#define REG_ESR    272
#define REG_FAR    280
#define REG_HPFAR  288

.section .text

/*
 * Exception vector table for EL2
 * Each entry is 128 bytes (0x80), 2KB aligned
 *
 * Vector offsets:
 *   0x000: Synchronous (Current EL, SP_EL0)
 *   0x080: IRQ (Current EL, SP_EL0)
 *   0x100: FIQ (Current EL, SP_EL0)
 *   0x180: SError (Current EL, SP_EL0)
 *   0x200: Synchronous (Current EL, SP_ELx)
 *   0x280: IRQ (Current EL, SP_ELx)
 *   0x300: FIQ (Current EL, SP_ELx)
 *   0x380: SError (Current EL, SP_ELx)
 *   0x400: Synchronous (Lower EL, AArch64)
 *   0x480: IRQ (Lower EL, AArch64)
 *   0x500: FIQ (Lower EL, AArch64)
 *   0x580: SError (Lower EL, AArch64)
 *   0x600: Synchronous (Lower EL, AArch32)
 *   0x680: IRQ (Lower EL, AArch32)
 *   0x700: FIQ (Lower EL, AArch32)
 *   0x780: SError (Lower EL, AArch32)
 */
.align 11
.globl _exception_vectors
_exception_vectors:
    /* Current EL, SP_EL0 */
    b _el2_sync_sp0          /* 0x000 */
    .align 7
    b _el2_irq_sp0           /* 0x080 */
    .align 7
    b _el2_fiq_sp0           /* 0x100 */
    .align 7
    b _el2_serror_sp0        /* 0x180 */
    .align 7
    /* Current EL, SP_ELx (this is our main path for EL2 traps) */
    b _el2_sync_spx          /* 0x200 */
    .align 7
    b _el2_irq_spx           /* 0x280 */
    .align 7
    b _el2_fiq_spx           /* 0x300 */
    .align 7
    b _el2_serror_spx        /* 0x380 */
    .align 7
    /* Lower EL, AArch64 (guest traps) */
    b _el1_sync              /* 0x400 */
    .align 7
    b _el1_irq               /* 0x480 */
    .align 7
    b _el1_fiq               /* 0x500 */
    .align 7
    b _el1_serror            /* 0x580 */
    .align 7
    /* Lower EL, AArch32 (not used) */
    b _unhandled             /* 0x600 */
    .align 7
    b _unhandled             /* 0x680 */
    .align 7
    b _unhandled             /* 0x700 */
    .align 7
    b _unhandled             /* 0x780 */

/* Unhandled exception - spin */
_unhandled:
    wfe
    b _unhandled

/* EL2 trap handlers (Current EL, SP_EL0) - should not happen, spin */
_el2_sync_sp0:
_el2_irq_sp0:
_el2_fiq_sp0:
_el2_serror_sp0:
    wfe
    b _el2_sync_sp0

/* EL2 sync (Current EL, SP_ELx) - hypervisor-mode sync trap */
_el2_sync_spx:
_el2_serror_spx:
    /* Save registers on the current (hypervisor) stack */
    sub  sp, sp, #REG_SIZE
    stp  x0, x1,   [sp, #REG_X0]
    stp  x2, x3,   [sp, #REG_X2]
    stp  x4, x5,   [sp, #REG_X4]
    stp  x6, x7,   [sp, #REG_X6]
    stp  x8, x9,   [sp, #REG_X8]
    stp  x10, x11, [sp, #REG_X10]
    stp  x12, x13, [sp, #REG_X12]
    stp  x14, x15, [sp, #REG_X14]
    stp  x16, x17, [sp, #REG_X16]
    stp  x18, x19, [sp, #REG_X18]
    stp  x20, x21, [sp, #REG_X20]
    stp  x22, x23, [sp, #REG_X22]
    stp  x24, x25, [sp, #REG_X24]
    stp  x26, x27, [sp, #REG_X26]
    stp  x28, x29, [sp, #REG_X28]
    str  x30,      [sp, #REG_X30]

    /* Save SP (the one before we subtracted) */
    add  x0, sp, #REG_SIZE
    str  x0,      [sp, #REG_SP]

    /* Save ELR, SPSR, ESR, FAR */
    mrs  x0, elr_el2
    str  x0, [sp, #REG_ELR]
    mrs  x0, spsr_el2
    str  x0, [sp, #REG_SPSR]
    mrs  x0, esr_el2
    str  x0, [sp, #REG_ESR]
    mrs  x0, far_el2
    str  x0, [sp, #REG_FAR]
    mrs  x0, hpfar_el2
    str  x0, [sp, #REG_HPFAR]

    /* Call C trap handler: aarch64_trap_handler(regs, from_guest=0) */
    mov  x0, sp
    mov  x1, #0
    bl   aarch64_trap_handler

    /* Restore and return */
    b    _trap_restore_el2

/* EL2 IRQ/FIQ (Current EL, SP_ELx) - hypervisor-mode IRQ */
_el2_irq_spx:
_el2_fiq_spx:
    /* Save registers on the current (hypervisor) stack */
    sub  sp, sp, #REG_SIZE
    stp  x0, x1,   [sp, #REG_X0]
    stp  x2, x3,   [sp, #REG_X2]
    stp  x4, x5,   [sp, #REG_X4]
    stp  x6, x7,   [sp, #REG_X6]
    stp  x8, x9,   [sp, #REG_X8]
    stp  x10, x11, [sp, #REG_X10]
    stp  x12, x13, [sp, #REG_X12]
    stp  x14, x15, [sp, #REG_X14]
    stp  x16, x17, [sp, #REG_X16]
    stp  x18, x19, [sp, #REG_X18]
    stp  x20, x21, [sp, #REG_X20]
    stp  x22, x23, [sp, #REG_X22]
    stp  x24, x25, [sp, #REG_X24]
    stp  x26, x27, [sp, #REG_X26]
    stp  x28, x29, [sp, #REG_X28]
    str  x30,      [sp, #REG_X30]

    /* Save SP (the one before we subtracted) */
    add  x0, sp, #REG_SIZE
    str  x0,      [sp, #REG_SP]

    /* Save ELR, SPSR, ESR, FAR */
    mrs  x0, elr_el2
    str  x0, [sp, #REG_ELR]
    mrs  x0, spsr_el2
    str  x0, [sp, #REG_SPSR]
    mrs  x0, esr_el2
    str  x0, [sp, #REG_ESR]
    mrs  x0, far_el2
    str  x0, [sp, #REG_FAR]
    mrs  x0, hpfar_el2
    str  x0, [sp, #REG_HPFAR]

    /* Call C trap handler: aarch64_trap_handler(regs, from_guest=2) */
    mov  x0, sp
    mov  x1, #2
    bl   aarch64_trap_handler

    /* Restore and return */
    b    _trap_restore_el2

/*
 * _el1_sync - Synchronous exception from lower EL (guest)
 * _el1_serror - SError from lower EL (guest)
 */
_el1_sync:
_el1_serror:
    /* Save all guest registers on the hypervisor stack */
    sub  sp, sp, #REG_SIZE
    stp  x0, x1,   [sp, #REG_X0]
    stp  x2, x3,   [sp, #REG_X2]
    stp  x4, x5,   [sp, #REG_X4]
    stp  x6, x7,   [sp, #REG_X6]
    stp  x8, x9,   [sp, #REG_X8]
    stp  x10, x11, [sp, #REG_X10]
    stp  x12, x13, [sp, #REG_X12]
    stp  x14, x15, [sp, #REG_X14]
    stp  x16, x17, [sp, #REG_X16]
    stp  x18, x19, [sp, #REG_X18]
    stp  x20, x21, [sp, #REG_X20]
    stp  x22, x23, [sp, #REG_X22]
    stp  x24, x25, [sp, #REG_X24]
    stp  x26, x27, [sp, #REG_X26]
    stp  x28, x29, [sp, #REG_X28]
    str  x30,      [sp, #REG_X30]

    /* Save guest SP_EL1 */
    mrs  x0, sp_el1
    str  x0, [sp, #REG_SP]

    /* Save ELR, SPSR, ESR, FAR, HPFAR */
    mrs  x0, elr_el2
    str  x0, [sp, #REG_ELR]
    mrs  x0, spsr_el2
    str  x0, [sp, #REG_SPSR]
    mrs  x0, esr_el2
    str  x0, [sp, #REG_ESR]
    mrs  x0, far_el2
    str  x0, [sp, #REG_FAR]
    mrs  x0, hpfar_el2
    str  x0, [sp, #REG_HPFAR]

    /* Call C trap handler: aarch64_trap_handler(regs, from_guest=1) */
    mov  x0, sp
    mov  x1, #1
    bl   aarch64_trap_handler

    /* Restore and return to guest */
    b    _trap_restore_guest

/*
 * _el1_irq  - IRQ from lower EL (guest)
 * _el1_fiq  - FIQ from lower EL (guest)
 */
_el1_irq:
_el1_fiq:
    /* Save all guest registers on the hypervisor stack */
    sub  sp, sp, #REG_SIZE
    stp  x0, x1,   [sp, #REG_X0]
    stp  x2, x3,   [sp, #REG_X2]
    stp  x4, x5,   [sp, #REG_X4]
    stp  x6, x7,   [sp, #REG_X6]
    stp  x8, x9,   [sp, #REG_X8]
    stp  x10, x11, [sp, #REG_X10]
    stp  x12, x13, [sp, #REG_X12]
    stp  x14, x15, [sp, #REG_X14]
    stp  x16, x17, [sp, #REG_X16]
    stp  x18, x19, [sp, #REG_X18]
    stp  x20, x21, [sp, #REG_X20]
    stp  x22, x23, [sp, #REG_X22]
    stp  x24, x25, [sp, #REG_X24]
    stp  x26, x27, [sp, #REG_X26]
    stp  x28, x29, [sp, #REG_X28]
    str  x30,      [sp, #REG_X30]

    /* Save guest SP_EL1 */
    mrs  x0, sp_el1
    str  x0, [sp, #REG_SP]

    /* Save ELR, SPSR, ESR, FAR, HPFAR */
    mrs  x0, elr_el2
    str  x0, [sp, #REG_ELR]
    mrs  x0, spsr_el2
    str  x0, [sp, #REG_SPSR]
    mrs  x0, esr_el2
    str  x0, [sp, #REG_ESR]
    mrs  x0, far_el2
    str  x0, [sp, #REG_FAR]
    mrs  x0, hpfar_el2
    str  x0, [sp, #REG_HPFAR]

    /* Call C trap handler: aarch64_trap_handler(regs, from_guest=3) */
    mov  x0, sp
    mov  x1, #3
    bl   aarch64_trap_handler

    /* Restore and return to guest */
    b    _trap_restore_guest

_trap_restore_el2:
    /* Restore ELR and SPSR for EL2 */
    ldr  x0, [sp, #REG_ELR]
    msr  elr_el2, x0
    ldr  x0, [sp, #REG_SPSR]
    msr  spsr_el2, x0

    /* Restore GP registers */
    ldp  x0, x1,   [sp, #REG_X0]
    ldp  x2, x3,   [sp, #REG_X2]
    ldp  x4, x5,   [sp, #REG_X4]
    ldp  x6, x7,   [sp, #REG_X6]
    ldp  x8, x9,   [sp, #REG_X8]
    ldp  x10, x11, [sp, #REG_X10]
    ldp  x12, x13, [sp, #REG_X12]
    ldp  x14, x15, [sp, #REG_X14]
    ldp  x16, x17, [sp, #REG_X16]
    ldp  x18, x19, [sp, #REG_X18]
    ldp  x20, x21, [sp, #REG_X20]
    ldp  x22, x23, [sp, #REG_X22]
    ldp  x24, x25, [sp, #REG_X24]
    ldp  x26, x27, [sp, #REG_X26]
    ldp  x28, x29, [sp, #REG_X28]
    ldr  x30,      [sp, #REG_X30]

    /* Restore SP */
    add  sp, sp, #REG_SIZE

    eret

_trap_restore_guest:
    /* Restore ELR and SPSR for guest */
    ldr  x0, [sp, #REG_ELR]
    msr  elr_el2, x0
    ldr  x0, [sp, #REG_SPSR]
    msr  spsr_el2, x0

    /* Restore guest SP_EL1 */
    ldr  x0, [sp, #REG_SP]
    msr  sp_el1, x0

    /* Restore GP registers */
    ldp  x0, x1,   [sp, #REG_X0]
    ldp  x2, x3,   [sp, #REG_X2]
    ldp  x4, x5,   [sp, #REG_X4]
    ldp  x6, x7,   [sp, #REG_X6]
    ldp  x8, x9,   [sp, #REG_X8]
    ldp  x10, x11, [sp, #REG_X10]
    ldp  x12, x13, [sp, #REG_X12]
    ldp  x14, x15, [sp, #REG_X14]
    ldp  x16, x17, [sp, #REG_X16]
    ldp  x18, x19, [sp, #REG_X18]
    ldp  x20, x21, [sp, #REG_X20]
    ldp  x22, x23, [sp, #REG_X22]
    ldp  x24, x25, [sp, #REG_X24]
    ldp  x26, x27, [sp, #REG_X26]
    ldp  x28, x29, [sp, #REG_X28]
    ldr  x30,      [sp, #REG_X30]

    add  sp, sp, #REG_SIZE

    eret

/*
 * leave_hypervisor_to_guest - Enter EL1 guest partition
 *
 * x0 = pointer to cpu_user_regs (saved guest state)
 */
.globl leave_hypervisor_to_guest
.type leave_hypervisor_to_guest, @function
.align 2
leave_hypervisor_to_guest:
    mov  sp, x0

    /* Restore ELR and SPSR */
    ldr  x0, [sp, #REG_ELR]
    msr  elr_el2, x0
    ldr  x0, [sp, #REG_SPSR]
    msr  spsr_el2, x0
    ldr  x0, [sp, #REG_SP]
    msr  sp_el1, x0

    /* Restore all GP regs */
    ldp  x0, x1,   [sp, #REG_X0]
    ldp  x2, x3,   [sp, #REG_X2]
    ldp  x4, x5,   [sp, #REG_X4]
    ldp  x6, x7,   [sp, #REG_X6]
    ldp  x8, x9,   [sp, #REG_X8]
    ldp  x10, x11, [sp, #REG_X10]
    ldp  x12, x13, [sp, #REG_X12]
    ldp  x14, x15, [sp, #REG_X14]
    ldp  x16, x17, [sp, #REG_X16]
    ldp  x18, x19, [sp, #REG_X18]
    ldp  x20, x21, [sp, #REG_X20]
    ldp  x22, x23, [sp, #REG_X22]
    ldp  x24, x25, [sp, #REG_X24]
    ldp  x26, x27, [sp, #REG_X26]
    ldp  x28, x29, [sp, #REG_X28]
    ldr  x30,      [sp, #REG_X30]

    add  sp, sp, #REG_SIZE

    eret
