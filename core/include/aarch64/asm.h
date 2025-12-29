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

/* context-switch */
#define CONTEXT_SWITCH(next_kthread, current_kthread)

#define JMP_PARTITION(entry, k)
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
