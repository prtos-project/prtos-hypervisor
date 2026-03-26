/*
 * FILE: asm.h
 *
 * assembly macros and functions
 *
 * www.prtos.org
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

// FIXME: here is just WA to make code work --------------------------------------------------------
#define PSR_IRQ_MASK (1U << 7) /* Interrupt mask */
#define PSR_FIQ_MASK (1U << 6) /* Fast Interrupt mask */
#define local_irq_disable() asm volatile("msr daifset, #2\n" ::: "memory")
#define local_irq_enable() asm volatile("msr daifclr, #2\n" ::: "memory")

#define local_save_flags(x) ({ asm volatile("mrs    %0, daif    // local_save_flags\n" : "=r"(x) : : "memory"); })

#define local_irq_save(x)    \
    ({                       \
        local_save_flags(x); \
        local_irq_disable(); \
    })

#define local_irq_restore(x) ({ asm volatile("msr    daif, %0                // local_irq_restore" : : "r"(x) : "memory"); })

static inline int local_irq_is_enabled(void) {
    prtos_word_t flags;
    local_save_flags(flags);
    return !(flags & PSR_IRQ_MASK);
}

static inline int local_fiq_is_enabled(void) {
    prtos_word_t flags;
    local_save_flags(flags);
    return !(flags & PSR_FIQ_MASK);
}

#define hw_cli() local_irq_disable()
#define hw_sti() local_irq_enable()
#define hw_restore_flags(flags) local_irq_restore(flags)
#define hw_save_flags(flags) local_save_flags(flags)
////// ----------------------------------------------------------

#define PUSH_REGISTERS

#define FP_OFFSET 108

#define PUSH_FP

#define POP_FP

#define POP_REGISTERS

/* context-switch: save callee-saved registers, swap stacks, restore.
 * _cs_next: next kthread to run; _cs_old: old kthread (unused, kept for API compat) */
#define CONTEXT_SWITCH(_cs_next, _cs_old)                                                                                                   \
    do {                                                                                                                                    \
        kthread_t **_cs_cur_ptr = &GET_LOCAL_PROCESSOR()->sched.current_kthread;                                                            \
        __asm__ __volatile__(                  /* Save return address as resume point */                                                    \
                             "adr x30, 1f\n\t" /* Push callee-saved registers: x19-x28, fp(x29), lr(x30) */                                 \
                             "stp x29, x30, [sp, #-16]!\n\t"                                                                                \
                             "stp x27, x28, [sp, #-16]!\n\t"                                                                                \
                             "stp x25, x26, [sp, #-16]!\n\t"                                                                                \
                             "stp x23, x24, [sp, #-16]!\n\t"                                                                                \
                             "stp x21, x22, [sp, #-16]!\n\t"                                                                                \
                             "stp x19, x20, [sp, #-16]!\n\t" /* Save sp into current kthread->ctrl.kstack */                                \
                             "ldr x10, [%1]\n\t"             /* x10 = *cur_ptr = old kthread */                                             \
                             "mov x11, sp\n\t"                                                                                              \
                             "str x11, [x10, #8]\n\t" /* kthread->ctrl.kstack = sp */ /* Load sp from next kthread->ctrl.kstack */          \
                             "ldr x11, [%0, #8]\n\t"                                  /* x11 = next->ctrl.kstack */                         \
                             "mov sp, x11\n\t"                                        /* Update current kthread pointer */                  \
                             "str %0, [%1]\n\t"                                       /* Restore callee-saved registers */                  \
                             "ldp x19, x20, [sp], #16\n\t"                                                                                  \
                             "ldp x21, x22, [sp], #16\n\t"                                                                                  \
                             "ldp x23, x24, [sp], #16\n\t"                                                                                  \
                             "ldp x25, x26, [sp], #16\n\t"                                                                                  \
                             "ldp x27, x28, [sp], #16\n\t"                                                                                  \
                             "ldp x29, x30, [sp], #16\n\t"                                                                                  \
                             "ret\n\t"                                                                                                      \
                             "1:\n\t"                                                                                                       \
                             :                                                                                                              \
                             : "r"(_cs_next), "r"(_cs_cur_ptr)                                                                              \
                             : "x10", "x11", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "memory"); \
    } while (0)

/* HCR_EL2 value for partitions:
 * RW(31) | TSC(19) | AMO(5) | IMO(4) | FMO(3) | VM(0)
 * TSC traps SMC to EL2 (needed for PSCI emulation for Linux). */
#define PRTOS_HCR_EL2_VAL 0x80080039ULL

/* Jump to partition at EL1 using ERET.
 * Passes the PCT physical address in x0 (same role as %ebx in x86).
 * Sets HCR_EL2: HCR_RW | HCR_TSC | HCR_AMO | HCR_IMO | HCR_FMO | HCR_VM */
#define JMP_PARTITION(entry, k)                                                                                          \
    do {                                                                                                                 \
        prtos_u64_t _pct_paddr = (prtos_u64_t)_VIRT2PHYS((prtos_address_t)(k)->ctrl.g->part_ctrl_table);                 \
        prtos_u64_t _hcr = PRTOS_HCR_EL2_VAL;                                                                           \
        /* IRQs must stay masked until ERET.  ERET atomically loads PSTATE from SPSR_EL2 (= 0x5 = EL1h,                 \
         * DAIF clear) so IRQs are enabled on arrival at EL1 — same semantics as x86 IRET.                              \
         * Enabling IRQs here would race with the timer: an IRQ between msr elr/spsr and eret                           \
         * corrupts the return state (the handler overwrites ELR/SPSR, and the final eret                               \
         * jumps to a stale address at EL2 instead of EL1). */                                                           \
        __asm__ __volatile__(                                                                                            \
                             "msr hcr_el2, %2\n\t"    /* HCR_EL2 value */                                               \
                             "msr elr_el2, %1\n\t"    /* entry point */                                                  \
                             "mov x10, #0x5\n\t"      /* SPSR_EL2: EL1h mode */                                          \
                             "msr spsr_el2, x10\n\t"  /* Disable EL1 stage-1 MMU so partition VA=IPA via stage-2 only */ \
                             "msr sctlr_el1, xzr\n\t" /* Enable FP/SIMD at EL1: CPACR_EL1.FPEN=0b11 (no trap) */         \
                             "mov x10, #(3 << 20)\n\t"                                                                   \
                             "msr cpacr_el1, x10\n\t"                                                                    \
                             "isb\n\t"                                                                                   \
                             "mov x0, %0\n\t" /* x0 = PCT physical address */                                            \
                             "isb\n\t"                                                                                   \
                             "eret\n\t"                                                                                  \
                             :                                                                                           \
                             : "r"(_pct_paddr), "r"((prtos_u64_t)(entry)),                                               \
                               "r"(_hcr)                                                                                 \
                             : "x0", "x10", "memory");                                                                   \
    } while (0)

/* Jump to partition at EL1 for PSCI secondary vCPU boot.
 * Passes context_id in x0 (per PSCI spec) and jumps to psci_entry.
 * SPSR = EL1h with DAIF masked (0x3c5). */
#define JMP_PARTITION_PSCI(k)                                                                                            \
    do {                                                                                                                 \
        prtos_u64_t _entry = (k)->ctrl.g->karch.psci_entry;                                                              \
        prtos_u64_t _ctx = (k)->ctrl.g->karch.psci_context_id;                                                           \
        prtos_u64_t _hcr = PRTOS_HCR_EL2_VAL;                                                                           \
        __asm__ __volatile__(                                                                                            \
                             "msr hcr_el2, %2\n\t"    /* HCR_EL2 value */                                               \
                             "msr elr_el2, %1\n\t"    /* PSCI entry point */                                             \
                             "mov x10, #0x3c5\n\t"    /* SPSR_EL2: EL1h, DAIF masked */                                  \
                             "msr spsr_el2, x10\n\t"                                                                     \
                             "msr sctlr_el1, xzr\n\t"                                                                   \
                             "mov x10, #(3 << 20)\n\t"                                                                   \
                             "msr cpacr_el1, x10\n\t"                                                                    \
                             "isb\n\t"                                                                                   \
                             "mov x0, %0\n\t" /* x0 = context_id */                                                      \
                             "mov x1, xzr\n\t"                                                                           \
                             "mov x2, xzr\n\t"                                                                           \
                             "mov x3, xzr\n\t"                                                                           \
                             "isb\n\t"                                                                                   \
                             "eret\n\t"                                                                                  \
                             :                                                                                           \
                             : "r"(_ctx), "r"(_entry),                                                                   \
                               "r"(_hcr)                                                                                 \
                             : "x0", "x1", "x2", "x3", "x10", "memory");                                                \
    } while (0)
#endif /*__ASSEMBLY__*/

#define load_seg_selector(_cs, _ds)

#define do_nop()

#define do_div(n, base)

#ifndef __ASSEMBLY__

static inline void save_fpu_state(prtos_u8_t *addr) {}

static inline void restore_fpu_state(prtos_u8_t *addr) {}

static inline prtos_word_t save_stack(void) {
    prtos_word_t sp;
    return sp;
}

static inline prtos_word_t save_bp(void) {
    prtos_word_t bp;
    return bp;
}

#define load_gdt(gdt)

#define load_idt(idt)
#define load_tr(seg)
#define DEC_LOAD_CR(_r)
#define DEC_SAVE_CR(_r)

DEC_LOAD_CR(0);
DEC_LOAD_CR(3);
DEC_LOAD_CR(4);

DEC_SAVE_CR(0);
DEC_SAVE_CR(2);
DEC_SAVE_CR(3);
DEC_SAVE_CR(4);

#define load_ptd_level1(ptd_level_1, _id)
#define load_hyp_page_table()
#define load_part_page_table(k)
#define load_gs(gs)

#define save_ss(ss)

#define save_cs(cs)

#define save_eip(eip)

static inline void flush_tlb(void) {}

static inline void flush_tlb_global(void) {}

#define disable_paging()
#define enable_paging()
static inline void flush_tlb_entry(prtos_address_t addr) {}

static inline prtos_u64_t read_tsc_load_low(void) {  // read time stamp counter - load low
    prtos_u32_t l, h;
    return (l | ((prtos_u64_t)h << 32));
}

static inline void read_tsc(prtos_u32_t *l, prtos_u32_t *h) {}

static inline prtos_u64_t read_msr(prtos_u32_t msr) {
    prtos_u32_t l, h;

    return (l | ((prtos_u64_t)h << 32));
}

static inline prtos_address_t save_gdt(void) {
    prtos_address_t gdt;
    return gdt;
}

static inline void write_msr(prtos_u32_t msr, prtos_u32_t l, prtos_u32_t h) {}

static inline void cpu_id(prtos_u32_t op, prtos_u32_t *eax, prtos_u32_t *ebx, prtos_u32_t *ecx, prtos_u32_t *edx) {
    *eax = op;
    *ecx = 0;
}

#define FNINIT()
#define CLTS()
static inline void set_wp(void) {
    prtos_u32_t tmpreg;
}

static inline void clear_wp(void) {
    prtos_u32_t tmpreg;
}

static inline prtos_s32_t asm_rw_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t aligment) {
    prtos_address_t addr;
    prtos_s32_t ret = 1;
    prtos_u8_t tmp;

    return 0;  // Have write permission
}

static inline prtos_s32_t asm_ronly_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t aligment) {
    prtos_address_t addr;
    prtos_s32_t ret = 1;
    prtos_u8_t tmp;

    return 0;  // Have write permission
}

#endif
#endif
#endif
