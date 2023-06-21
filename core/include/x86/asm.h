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

#define PUSH_REGISTERS \
    "pushl %%eax\n\t"  \
    "pushl %%ebp\n\t"  \
    "pushl %%edi\n\t"  \
    "pushl %%esi\n\t"  \
    "pushl %%edx\n\t"  \
    "pushl %%ecx\n\t"  \
    "pushl %%ebx\n\t"

#define FP_OFFSET 108

#define PUSH_FP                                    \
    "sub $" TO_STR(FP_OFFSET) ",%%esp\n\t"         \
                              "fnsave (%%esp)\n\t" \
                              "fwait\n\t"

#define POP_FP           \
    "frstor (%%esp)\n\t" \
    "add $" TO_STR(FP_OFFSET) ", %%esp\n\t"

#define POP_REGISTERS \
    "popl %%ebx\n\t"  \
    "popl %%ecx\n\t"  \
    "popl %%edx\n\t"  \
    "popl %%esi\n\t"  \
    "popl %%edi\n\t"  \
    "popl %%ebp\n\t"  \
    "popl %%eax\n\t"

/* context-switch */
#define CONTEXT_SWITCH(next_kthread, current_kthread)                               \
    __asm__ __volatile__(PUSH_REGISTERS                                             \
                        "movl (%%ebx), %%edx\n\t"                                   \
                        "pushl $1f\n\t"                                             \
                        "movl %%esp, " TO_STR(_KSTACK_OFFSET) "(%%edx)\n\t"         \
                        "movl " TO_STR(_KSTACK_OFFSET) "(%%ecx), %%esp\n\t"         \
                        "movl %%ecx, (%%ebx)\n\t"                                   \
                        "ret\n\t"                                                   \
                        "1:\n\t" POP_REGISTERS                                      \
                         :                                                          \
                         : "c"(next_kthread), "b"(current_kthread))

#define JMP_PARTITION(entry, k)                                     \
    __asm__ __volatile__(                                           \
        "pushl $" TO_STR(GUEST_DS_SEL) "\n\t" /* SS */              \
        "pushl $0\n\t" /* ESP */                                    \
        "pushl $" TO_STR(_CPU_FLAG_IF | 0x2) "\n\t" /* EFLAGS */    \
        "pushl $" TO_STR(GUEST_CS_SEL) "\n\t" /* CS */              \
        "pushl %0\n\t" /* EIP */                                    \
        "movl $" TO_STR(GUEST_DS_SEL) ", %%edx\n\t"                 \
        "movl %%edx, %%ds\n\t"                                      \
        "movl %%edx, %%es\n\t"                                      \
        "xorl %%edx, %%edx\n\t"                                     \
        "movl %%edx, %%fs\n\t"                                      \
        "movl %%edx, %%gs\n\t"                                      \
        "movl %%edx, %%ebp\n\t"                                     \
        "iret"                                                      \
        :                                                           \
        : "r"(entry), "b"(PRTOS_PCTRLTAB_ADDR)                      \
        : "edx")

#endif /*__ASSEMBLY__*/

#define load_seg_selector(_cs, _ds)                                                       \
    __asm__ __volatile__("ljmp $" TO_STR(_cs) ", $1f\n\t"                                 \
                                              "1:\n\t"                                    \
                                              "movl $(" TO_STR(_ds) "), %%eax\n\t"        \
                                                                    "mov %%eax, %%ds\n\t" \
                                                                    "mov %%eax, %%es\n\t" \
                                                                    "mov %%eax, %%ss\n\t" ::)

#define do_nop() __asm__ __volatile__("nop\n\t" ::)

#define do_div(n, base)                                                                     \
    ({                                                                                      \
        prtos_u32_t __upper, __low, __high, __mod, __base;                                  \
        __base = (base);                                                                    \
        asm("" : "=a"(__low), "=d"(__high) : "A"(n));                                       \
        __upper = __high;                                                                   \
        if (__high) {                                                                       \
            __upper = __high % (__base);                                                    \
            __high = __high / (__base);                                                     \
        }                                                                                   \
        asm("divl %2" : "=a"(__low), "=d"(__mod) : "rm"(__base), "0"(__low), "1"(__upper)); \
        asm("" : "=A"(n) : "a"(__low), "d"(__high));                                        \
        __mod;                                                                              \
    })

#ifndef __ASSEMBLY__

static inline void save_fpu_state(prtos_u8_t *addr) {
    __asm__ __volatile__("fnsave (%0)\n\t"
                         "fwait\n\t"
                         :
                         : "r"(addr));
}

static inline void restore_fpu_state(prtos_u8_t *addr) {
    __asm__ __volatile__("frstor (%0)\n\t" : : "r"(addr));
}

static inline prtos_word_t save_stack(void) {
    prtos_word_t sp;
    __asm__ __volatile__("movl %%esp, %0\n\t" : "=r"(sp)::"memory");
    return sp;
}

static inline prtos_word_t save_bp(void) {
    prtos_word_t bp;
    __asm__ __volatile__("movl %%ebp, %0\n\t" : "=a"(bp));
    return bp;
}

#define hw_cli() __asm__ __volatile__("cli\n\t" ::: "memory")
#define hw_sti() __asm__ __volatile__("sti\n\t" ::: "memory")

#define hw_restore_flags(flags)        \
    __asm__ __volatile__("push %0\n\t" \
                         "popf\n\t"    \
                         :             \
                         : "g"(flags)  \
                         : "memory", "cc")

#define hw_save_flags(flags)            \
    __asm__ __volatile__("pushf\n\t"    \
                         "pop %0\n\t"   \
                         : "=rm"(flags) \
                         :              \
                         : "memory")

#define load_gdt(gdt) __asm__ __volatile__("lgdt %0\n\t" : : "m"(gdt))

#define load_idt(idt) __asm__ __volatile__("lidt %0\n\t" : : "m"(idt))

#define load_tr(seg) __asm__ __volatile__("ltr %0\n\t" : : "rm"((prtos_u16_t)(seg)))

#define DEC_LOAD_CR(_r)                                                                \
    static inline void load_cr##_r(prtos_u32_t cr##_r) {                               \
        __asm__ __volatile__("mov %0, %%cr" #_r "\n\t" : : "r"((prtos_word_t)cr##_r)); \
    }

#define DEC_SAVE_CR(_r)                                                 \
    static inline prtos_word_t save_cr##_r(void) {                      \
        prtos_word_t cr##_r;                                            \
        __asm__ __volatile__("mov %%cr" #_r ", %0\n\t" : "=r"(cr##_r)); \
        return cr##_r;                                                  \
    }

DEC_LOAD_CR(0);
DEC_LOAD_CR(3);
DEC_LOAD_CR(4);

DEC_SAVE_CR(0);
DEC_SAVE_CR(2);
DEC_SAVE_CR(3);
DEC_SAVE_CR(4);

#define load_ptd_level1(ptd_level_1, _id) load_cr3(ptd_level_1)
#define load_hyp_page_table() load_cr3(_VIRT2PHYS((prtos_u32_t)_page_tables))

#define load_part_page_table(k) load_cr3(k->ctrl.g->karch.ptd_level_1)

#define load_gs(gs)                                           \
    do {                                                      \
        __asm__ __volatile__("mov %0, %%gs\n\t" : : "r"(gs)); \
    } while (0)

#define save_ss(ss)                                          \
    do {                                                     \
        __asm__ __volatile__("mov %%ss, %0\n\t" : "=a"(ss)); \
    } while (0)

#define save_cs(cs)                                          \
    do {                                                     \
        __asm__ __volatile__("mov %%cs, %0\n\t" : "=a"(cs)); \
    } while (0)

#define save_eip(eip)                           \
    do {                                        \
        __asm__ __volatile__("movl $1f, %0\n\t" \
                             "1:\n\t"           \
                             : "=r"(eip));      \
    } while (0)

static inline void flush_tlb(void) {
    load_cr3(save_cr3());
}

static inline void flush_tlb_global(void) {
    prtos_word_t cr4;
    cr4 = save_cr4();
    load_cr4(cr4 & ~_CR4_PGE);
    flush_tlb();
    load_cr4(cr4);
}

#define disable_paging() load_cr0(save_cr0() | _CR0_PG);
#define enable_paging() load_cr0(save_cr0() & ~_CR0_PG)

static inline void flush_tlb_entry(prtos_address_t addr) {
    __asm__ __volatile__("invlpg (%0)" ::"r"(addr) : "memory");
}

static inline prtos_u64_t read_tsc_load_low(void) {  // read time stamp counter - load low
    prtos_u32_t l, h;
    __asm__ __volatile__("rdtsc" : "=a"(l), "=d"(h));
    return (l | ((prtos_u64_t)h << 32));
}

static inline void read_tsc(prtos_u32_t *l, prtos_u32_t *h) {
    __asm__ __volatile__("rdtsc" : "=a"(*l), "=d"(*h));
}

static inline prtos_u64_t read_msr(prtos_u32_t msr) {
    prtos_u32_t l, h;

    __asm__ __volatile__("rdmsr\n\t" : "=a"(l), "=d"(h) : "c"(msr));
    return (l | ((prtos_u64_t)h << 32));
}

static inline prtos_address_t save_gdt(void) {
    prtos_address_t gdt;
    __asm__ __volatile__("sgdt %0\n\t" : "=m"(gdt));
    return gdt;
}

static inline void write_msr(prtos_u32_t msr, prtos_u32_t l, prtos_u32_t h) {
    __asm__ __volatile__("wrmsr\n\t" ::"c"(msr), "a"(l), "d"(h) : "memory");
}

static inline void cpu_id(prtos_u32_t op, prtos_u32_t *eax, prtos_u32_t *ebx, prtos_u32_t *ecx, prtos_u32_t *edx) {
    *eax = op;
    *ecx = 0;
    __asm__ __volatile__("cpuid\n\t" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "0"(*eax), "2"(*ecx));
}

#define FNINIT() __asm__ __volatile__("fninit\n\t" ::)
#define CLTS() __asm__ __volatile__("clts\n\t" ::)

static inline void set_wp(void) {
    prtos_u32_t tmpreg;
    __asm__ __volatile__("movl %%cr0, %0\n\t" : "=r"(tmpreg));
    tmpreg |= _CR0_WP;
    __asm__ __volatile__("movl %0, %%cr0\n\t" : : "r"(tmpreg));
}

static inline void clear_wp(void) {
    prtos_u32_t tmpreg;
    __asm__ __volatile__("movl %%cr0, %0\n\t" : "=r"(tmpreg));
    tmpreg &= (~_CR0_WP);
    __asm__ __volatile__("movl %0, %%cr0\n\t" : : "r"(tmpreg));
}

static inline prtos_s32_t asm_rw_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t aligment) {
    prtos_address_t addr;
    prtos_s32_t ret = 1;
    prtos_u8_t tmp;

    for (addr = param; addr < param + size; addr = (addr & PAGE_MASK) + PAGE_SIZE) {
        __asm__ __volatile__("1: movb %2, %1\n\t"
                             "2: movb %1, %2\n\t"
                             "xorl %0, %0\n\t"
                             "3:\n\t" ASM_EXPTABLE(1b, 3b) ASM_EXPTABLE(2b, 3b)
                             : "=b"(ret), "=c"(tmp)
                             : "m"(*(prtos_u8_t *)addr));
        if (ret) { // No write permission
            return -1;
        }
    }

    return 0; // Have write permission
}

static inline prtos_s32_t asm_ronly_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t aligment) {
    prtos_address_t addr;
    prtos_s32_t ret = 1;
    prtos_u8_t tmp;

    for (addr = param; addr < param + size; addr = (addr & PAGE_MASK) + PAGE_SIZE) {
        __asm__ __volatile__("1: movb %2, %1\n\t"
                             "xorl %0, %0\n\t"
                             "2:\n\t" ASM_EXPTABLE(1b, 2b)
                             : "=b"(ret), "=c"(tmp)
                             : "m"(*(prtos_u8_t *)addr));
        if (ret) { // No write permission
            return -1;
        }
    }

    return 0; // Have write permission
}

#endif
#endif
#endif
