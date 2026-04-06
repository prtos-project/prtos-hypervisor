/*
 * FILE: asm.h
 *
 * AArch64 assembly macros and functions
 *
 * http://www.prtos.org/
 */

#ifndef _PRTOS_ARCH_ASM_H_
#define _PRTOS_ARCH_ASM_H_

#ifdef _PRTOS_KERNEL_

#ifndef _GENERATE_OFFSETS_
#include <arch/asm_offsets.h>
#endif

#include <arch/processor.h>
#include <arch/paging.h>

#ifndef __ASSEMBLY__

#define ASM_EXPTABLE(_a, _b)        \
    ".section .exptable, \"a\"\n\t" \
    ".align 4\n\t"                  \
    ".long " #_a "\n\t"             \
    ".long " #_b "\n\t"             \
    ".previous\n\t"

/* AArch64 interrupt enable/disable via DAIF */
#define DAIF_I (1UL << 7)  /* IRQ mask */
#define DAIF_F (1UL << 6)  /* FIQ mask */

#define local_irq_disable() asm volatile("msr daifset, #2\n" ::: "memory")
#define local_irq_enable()  asm volatile("msr daifclr, #2\n" ::: "memory")

#define local_save_flags(x) ({ asm volatile("mrs %0, daif" : "=r"(x) :  : "memory"); })

#define local_irq_save(x)    \
    ({                       \
        local_save_flags(x); \
        local_irq_disable(); \
    })

#define local_irq_restore(x) ({ asm volatile("msr daif, %0" : : "r"(x) : "memory"); })

static inline int local_irq_is_enabled(void) {
    prtos_word_t flags;
    local_save_flags(flags);
    return !(flags & DAIF_I);
}

#define hw_cli() local_irq_disable()
#define hw_sti() local_irq_enable()
#define hw_restore_flags(flags) local_irq_restore(flags)
#define hw_save_flags(flags) local_save_flags(flags)

#define PUSH_REGISTERS
#define FP_OFFSET 108
#define PUSH_FP
#define POP_FP
#define POP_REGISTERS

/* context-switch: save callee-saved registers, swap stacks, restore.
 * AArch64 callee-saved: x19-x28, x29 (FP), x30 (LR), sp */
#define CONTEXT_SWITCH(_cs_next, _cs_old)                                                                                     \
    do {                                                                                                                      \
        kthread_t **_cs_cur_ptr = &GET_LOCAL_PROCESSOR()->sched.current_kthread;                                              \
        __asm__ __volatile__(                                                                                                 \
            "adr x9, 1f\n\t"                                                                                                  \
            /* Push callee-saved: x19-x28, x29 (FP), x30 (LR) = 12 regs, 96 bytes + 16 pad = 112 */                         \
            "sub sp, sp, #112\n\t"                                                                                            \
            "stp x19, x20, [sp, #0]\n\t"                                                                                     \
            "stp x21, x22, [sp, #16]\n\t"                                                                                    \
            "stp x23, x24, [sp, #32]\n\t"                                                                                    \
            "stp x25, x26, [sp, #48]\n\t"                                                                                    \
            "stp x27, x28, [sp, #64]\n\t"                                                                                    \
            "stp x29, x9,  [sp, #80]\n\t"  /* x29=FP, x9=resume PC stored as LR */                                          \
            "str x30,      [sp, #96]\n\t"  /* save real LR */                                                                \
            /* Save old kthread sp: old->ctrl.kstack = sp */                                                                  \
            "ldr x10, [%[cur]]\n\t"        /* x10 = *_cs_cur_ptr (old kthread) */                                            \
            "mov x11, sp\n\t"                                                                                                \
            "str x11, [x10, %[kstack_off]]\n\t"                                                                              \
            /* Switch to new kthread */                                                                                       \
            "str %[next], [%[cur]]\n\t"    /* *_cs_cur_ptr = _cs_next */                                                     \
            "ldr x11, [%[next], %[kstack_off]]\n\t"  /* x11 = new->ctrl.kstack */                                            \
            "mov sp, x11\n\t"                                                                                                \
            /* Pop callee-saved of new kthread */                                                                             \
            "ldp x19, x20, [sp, #0]\n\t"                                                                                     \
            "ldp x21, x22, [sp, #16]\n\t"                                                                                    \
            "ldp x23, x24, [sp, #32]\n\t"                                                                                    \
            "ldp x25, x26, [sp, #48]\n\t"                                                                                    \
            "ldp x27, x28, [sp, #64]\n\t"                                                                                    \
            "ldp x29, x9,  [sp, #80]\n\t"                                                                                    \
            "ldr x30,      [sp, #96]\n\t"                                                                                    \
            "add sp, sp, #112\n\t"                                                                                            \
            "br x9\n\t"                    /* Jump to new kthread's resume point */                                           \
            "1:\n\t"                                                                                                          \
            :                                                                                                                 \
            : [next] "r"(_cs_next),                                                                                           \
              [cur] "r"(_cs_cur_ptr),                                                                                         \
              [kstack_off] "i"(__builtin_offsetof(kthread_t, ctrl) + __builtin_offsetof(struct __kthread, kstack))             \
            : "memory", "x9", "x10", "x11", "x30",                                                                           \
              "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26",                                                       \
              "x27", "x28", "x29"                                                                                             \
        );                                                                                                                    \
    } while (0)

#define hw_is_sti() local_irq_is_enabled()

/* Idle hint: use CNTV (virtual timer) to generate a WFI wake event.
 * QEMU does not handle CNTHP_EL2 for WFI wakeup.  Guest CNTV state
 * is saved/restored in switch_kthread_arch_pre/post. */
#define do_nop() do { \
    prtos_u64_t _cnt; \
    __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(_cnt)); \
    _cnt += 625000; /* ~10ms at 62.5MHz */ \
    __asm__ __volatile__( \
        "msr cntv_cval_el0, %0\n\t" \
        "mov x10, #1\n\t" \
        "msr cntv_ctl_el0, x10\n\t" \
        "isb\n\t" \
        "wfi\n\t" \
        "msr cntv_ctl_el0, xzr\n\t" \
        "isb\n\t" \
        : : "r"(_cnt) : "x10", "memory"); \
} while (0)

static inline prtos_s32_t asm_rw_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t alignment) {
    return 0;
}

static inline prtos_s32_t asm_ronly_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t alignment) {
    return 0;
}

/* VTTBR_EL2 is loaded in switch_kthread_arch_post; no explicit page table load needed */
#define load_part_page_table(k) do {} while (0)

/* HCR_EL2 value for partitions: RW|TSC|AMO|IMO|FMO|SWIO|VM */
#define PRTOS_HCR_EL2_VAL 0x8008003bULL

/* Jump to partition entry point via eret into EL1.
 * x0 = PCT physical address, entry = partition entry point.
 * Uses VHE: set SPSR_EL2.M[3:0] = 0b0101 (EL1h). */
#define JMP_PARTITION(entry, k)                                                                          \
    do {                                                                                                 \
        prtos_u64_t _pct_paddr = (prtos_u64_t)_VIRT2PHYS((prtos_address_t)(k)->ctrl.g->part_ctrl_table); \
        prtos_u64_t _entry = (prtos_u64_t)(entry);                                                      \
        __asm__ __volatile__(                                                                            \
            /* Set ELR_EL2 = partition entry point */                                                    \
            "msr elr_el2, %1\n\t"                                                                        \
            /* Set SPSR_EL2: DAIF clear, M[3:0]=0101 (EL1h), NZCV=0 */                                  \
            "mov x0, #0x5\n\t"          /* EL1h */                                                       \
            "orr x0, x0, #(0xF << 6)\n\t" /* Mask DAIF initially */                                     \
            "bic x0, x0, #(0xF << 6)\n\t" /* Clear: let guest enable IRQs itself */                      \
            "mov x0, #0x3c5\n\t"        /* EL1h + DAIF masked */                                         \
            "msr spsr_el2, x0\n\t"                                                                       \
            /* Set HCR_EL2: RW|TSC|AMO|IMO|FMO|SWIO|VM */                                              \
            "mov x0, #0x80000000\n\t"   /* RW=1 (EL1 is AArch64) */                                      \
            "movk x0, #0x003b, lsl #0\n\t" /* VM|SWIO|FMO|IMO|AMO */                                    \
            "orr x0, x0, #(1 << 19)\n\t"   /* TSC: trap SMC to EL2 */                                   \
            "msr hcr_el2, x0\n\t"                                                                        \
            /* Clean guest EL1 system registers */                                                       \
            "msr sctlr_el1, xzr\n\t"                                                                     \
            "msr ttbr0_el1, xzr\n\t"                                                                     \
            "msr ttbr1_el1, xzr\n\t"                                                                     \
            "msr tcr_el1, xzr\n\t"                                                                       \
            "msr mair_el1, xzr\n\t"                                                                      \
            "msr vbar_el1, xzr\n\t"                                                                      \
            /* Enable FP/SIMD at EL1: CPACR_EL1.FPEN = 0b11 (bits [21:20]) */                           \
            "mov x0, #(3 << 20)\n\t"                                                                     \
            "msr cpacr_el1, x0\n\t"                                                                      \
            "isb\n\t"                                                                                    \
            /* Set sscratch = current kernel sp for trap reentry.                                         \
             * On AArch64 we use SP_EL2 (auto-selected) so just ensure it's set. */                      \
            /* x0 = PCT physical address, x1 = 0 (compatible with BAIL and Linux) */                     \
            "mov x0, %0\n\t"                                                                              \
            "mov x1, #0\n\t"                                                                              \
            "isb\n\t"                                                                                    \
            /* Enter EL1 */                                                                              \
            "eret\n\t"                                                                                   \
            :                                                                                            \
            : "r"(_pct_paddr), "r"(_entry)                                                               \
            : "x0", "x1", "memory");                                                                     \
    } while (0)

/* Jump to partition for PSCI secondary vCPU boot.
 * x0 = context_id (from PSCI CPU_ON), entry_point = target address. */
#define JMP_PARTITION_PSCI(k)                                                                            \
    do {                                                                                                 \
        prtos_u64_t _entry = (k)->ctrl.g->karch.psci_entry;                                              \
        prtos_u64_t _context_id = (k)->ctrl.g->karch.psci_context_id;                                    \
        prtos_u64_t _mpidr = (prtos_u64_t)KID2VCPUID((k)->ctrl.g->id);                                  \
        __asm__ __volatile__(                                                                            \
            "msr elr_el2, %0\n\t"                                                                        \
            "mov x2, #0x3c5\n\t"                                                                         \
            "msr spsr_el2, x2\n\t"                                                                       \
            "mov x2, #0x80000000\n\t"                                                                    \
            "movk x2, #0x003b, lsl #0\n\t"                                                              \
            "orr x2, x2, #(1 << 19)\n\t"   /* TSC: trap SMC to EL2 */                                   \
            "msr hcr_el2, x2\n\t"                                                                        \
            "msr sctlr_el1, xzr\n\t"                                                                     \
            "msr ttbr0_el1, xzr\n\t"                                                                     \
            "msr ttbr1_el1, xzr\n\t"                                                                     \
            "msr tcr_el1, xzr\n\t"                                                                       \
            "msr mair_el1, xzr\n\t"                                                                      \
            "msr vbar_el1, xzr\n\t"                                                                      \
            "isb\n\t"                                                                                    \
            "mov x0, %1\n\t"             /* context_id */                                                \
            "isb\n\t"                                                                                    \
            "eret\n\t"                                                                                   \
            :                                                                                            \
            : "r"(_entry), "r"(_context_id)                                                              \
            : "x0", "x2", "memory");                                                                     \
    } while (0)

#endif /* __ASSEMBLY__ */
#endif /* _PRTOS_KERNEL_ */
#endif
