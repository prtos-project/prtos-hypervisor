/*
 * FILE: asm.h
 *
 * Assembly macros and functions for amd64
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
    ".align 8\n\t"                  \
    ".quad " #_a "\n\t"             \
    ".quad " #_b "\n\t"             \
    ".previous\n\t"

#define PUSH_REGISTERS  \
    "pushq %%rax\n\t"  \
    "pushq %%rbp\n\t"  \
    "pushq %%rdi\n\t"  \
    "pushq %%rsi\n\t"  \
    "pushq %%rdx\n\t"  \
    "pushq %%rcx\n\t"  \
    "pushq %%rbx\n\t"  \
    "pushq %%r8\n\t"   \
    "pushq %%r9\n\t"   \
    "pushq %%r10\n\t"  \
    "pushq %%r11\n\t"  \
    "pushq %%r12\n\t"  \
    "pushq %%r13\n\t"  \
    "pushq %%r14\n\t"  \
    "pushq %%r15\n\t"

#define POP_REGISTERS   \
    "popq %%r15\n\t"   \
    "popq %%r14\n\t"   \
    "popq %%r13\n\t"   \
    "popq %%r12\n\t"   \
    "popq %%r11\n\t"   \
    "popq %%r10\n\t"   \
    "popq %%r9\n\t"    \
    "popq %%r8\n\t"    \
    "popq %%rbx\n\t"   \
    "popq %%rcx\n\t"   \
    "popq %%rdx\n\t"   \
    "popq %%rsi\n\t"   \
    "popq %%rdi\n\t"   \
    "popq %%rbp\n\t"   \
    "popq %%rax\n\t"

/* context-switch */
#define CONTEXT_SWITCH(next_kthread, current_kthread)                            \
    __asm__ __volatile__(PUSH_REGISTERS                                          \
                        "movq (%%rbx), %%rdx\n\t"                               \
                        "leaq 1f(%%rip), %%rax\n\t"                             \
                        "pushq %%rax\n\t"                                       \
                        "movq %%rsp, " TO_STR(_KSTACK_OFFSET) "(%%rdx)\n\t"     \
                        "movq " TO_STR(_KSTACK_OFFSET) "(%%rcx), %%rsp\n\t"     \
                        "movq %%rcx, (%%rbx)\n\t"                               \
                        "ret\n\t"                                               \
                        "1:\n\t" POP_REGISTERS                                  \
                         :                                                      \
                         : "c"(next_kthread), "b"(current_kthread))

#define JMP_PARTITION(entry, k)                                     \
    __asm__ __volatile__(                                           \
        "pushq $" TO_STR(GUEST_DS_SEL) "\n\t"                      \
        "pushq $0\n\t"                                              \
        "pushq $" TO_STR(_CPU_FLAG_IF | 0x2) "\n\t"                 \
        "pushq $" TO_STR(GUEST_CS_SEL) "\n\t"                      \
        "pushq %0\n\t"                                              \
        "movl $" TO_STR(GUEST_DS_SEL) ", %%edx\n\t"                 \
        "movw %%dx, %%ds\n\t"                                       \
        "movw %%dx, %%es\n\t"                                       \
        "xorl %%edx, %%edx\n\t"                                     \
        "movw %%dx, %%fs\n\t"                                       \
        "movw %%dx, %%gs\n\t"                                       \
        "xorq %%rbp, %%rbp\n\t"                                     \
        "iretq"                                                     \
        :                                                           \
        : "r"((prtos_u64_t)(entry)), "b"((prtos_u64_t)PRTOS_PCTRLTAB_ADDR) \
        : "rdx")

#endif /*__ASSEMBLY__*/

#define load_seg_selector(_cs, _ds)                                          \
    __asm__ __volatile__("pushq $" TO_STR(_cs) "\n\t"                        \
                         "leaq 1f(%%rip), %%rax\n\t"                         \
                         "pushq %%rax\n\t"                                   \
                         "lretq\n\t"                                         \
                         "1:\n\t"                                            \
                         "movl $(" TO_STR(_ds) "), %%eax\n\t"                \
                         "mov %%ax, %%ds\n\t"                                \
                         "mov %%ax, %%es\n\t"                                \
                         "mov %%ax, %%ss\n\t" ::: "rax")

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

static inline prtos_word_t save_stack(void) {
    prtos_word_t sp;
    __asm__ __volatile__("movq %%rsp, %0\n\t" : "=r"(sp)::"memory");
    return sp;
}

static inline prtos_word_t save_bp(void) {
    prtos_word_t bp;
    __asm__ __volatile__("movq %%rbp, %0\n\t" : "=r"(bp));
    return bp;
}

#define hw_cli() __asm__ __volatile__("cli\n\t" ::: "memory")
#define hw_sti() __asm__ __volatile__("sti\n\t" ::: "memory")

#define hw_restore_flags(flags)        \
    __asm__ __volatile__("pushq %0\n\t" \
                         "popfq\n\t"    \
                         :             \
                         : "g"((prtos_u64_t)(flags))  \
                         : "memory", "cc")

#define hw_save_flags(flags)            \
    __asm__ __volatile__("pushfq\n\t"    \
                         "popq %0\n\t"   \
                         : "=rm"(flags) \
                         :              \
                         : "memory")

#define load_gdt(gdt) __asm__ __volatile__("lgdt %0\n\t" : : "m"(gdt))

#define load_idt(idt) __asm__ __volatile__("lidt %0\n\t" : : "m"(idt))

#define load_tr(seg) __asm__ __volatile__("ltr %0\n\t" : : "rm"((prtos_u16_t)(seg)))

static inline void load_cr0(prtos_u64_t cr0) {
    __asm__ __volatile__("movq %0, %%cr0\n\t" : : "r"(cr0));
}

static inline void load_cr3(prtos_u64_t cr3) {
    __asm__ __volatile__("movq %0, %%cr3\n\t" : : "r"(cr3));
}

static inline void load_cr4(prtos_u64_t cr4) {
    __asm__ __volatile__("movq %0, %%cr4\n\t" : : "r"(cr4));
}

static inline prtos_u64_t save_cr0(void) {
    prtos_u64_t cr0;
    __asm__ __volatile__("movq %%cr0, %0\n\t" : "=r"(cr0));
    return cr0;
}

static inline prtos_u64_t save_cr2(void) {
    prtos_u64_t cr2;
    __asm__ __volatile__("movq %%cr2, %0\n\t" : "=r"(cr2));
    return cr2;
}

static inline prtos_u64_t save_cr3(void) {
    prtos_u64_t cr3;
    __asm__ __volatile__("movq %%cr3, %0\n\t" : "=r"(cr3));
    return cr3;
}

static inline prtos_u64_t save_cr4(void) {
    prtos_u64_t cr4;
    __asm__ __volatile__("movq %%cr4, %0\n\t" : "=r"(cr4));
    return cr4;
}

#define load_ptd_level1(ptd_level_1, _id) load_cr3(ptd_level_1)
#define load_hyp_page_table() load_cr3(_VIRT2PHYS((prtos_u64_t)(unsigned long)_page_tables))

#define load_part_page_table(k) load_cr3(k->ctrl.g->karch.ptd_level_1)

#define load_gs(gs)                                           \
    do {                                                      \
        __asm__ __volatile__("mov %0, %%gs\n\t" : : "r"((prtos_u16_t)(gs))); \
    } while (0)

#define save_ss(ss)                                          \
    do {                                                     \
        __asm__ __volatile__("mov %%ss, %0\n\t" : "=r"(ss)); \
    } while (0)

#define save_cs(cs)                                          \
    do {                                                     \
        __asm__ __volatile__("mov %%cs, %0\n\t" : "=r"(cs)); \
    } while (0)

#define save_rip(rip)                           \
    do {                                        \
        __asm__ __volatile__("leaq 1f(%%rip), %0\n\t" \
                             "1:\n\t"           \
                             : "=r"(rip));      \
    } while (0)

static inline void flush_tlb(void) {
    load_cr3(save_cr3());
}

static inline void flush_tlb_global(void) {
    prtos_u64_t cr4;
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

static inline prtos_u64_t read_tsc_load_low(void) {
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
    prtos_u64_t tmpreg;
    __asm__ __volatile__("movq %%cr0, %0\n\t" : "=r"(tmpreg));
    tmpreg |= _CR0_WP;
    __asm__ __volatile__("movq %0, %%cr0\n\t" : : "r"(tmpreg));
}

static inline void clear_wp(void) {
    prtos_u64_t tmpreg;
    __asm__ __volatile__("movq %%cr0, %0\n\t" : "=r"(tmpreg));
    tmpreg &= (~_CR0_WP);
    __asm__ __volatile__("movq %0, %%cr0\n\t" : : "r"(tmpreg));
}

static inline prtos_s32_t asm_rw_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t alignment) {
    prtos_address_t addr;
    prtos_s32_t ret = 1;
    prtos_u8_t tmp;

    for (addr = param; addr < param + size; addr = (addr & PAGE_MASK) + PAGE_SIZE) {
        __asm__ __volatile__("1: movb %2, %1\n\t"
                             "2: movb %1, %2\n\t"
                             "xorl %0, %0\n\t"
                             "3:\n\t" ASM_EXPTABLE(1b, 3b) ASM_EXPTABLE(2b, 3b)
                             : "=b"(ret), "=c"(tmp)
                             : "m"(*(prtos_u8_t *)(unsigned long)addr));
        if (ret) return -1;
    }
    return 0;
}

static inline prtos_s32_t asm_ronly_check(prtos_address_t param, prtos_u_size_t size, prtos_u32_t alignment) {
    prtos_address_t addr;
    prtos_s32_t ret = 1;
    prtos_u8_t tmp;

    for (addr = param; addr < param + size; addr = (addr & PAGE_MASK) + PAGE_SIZE) {
        __asm__ __volatile__("1: movb %2, %1\n\t"
                             "xorl %0, %0\n\t"
                             "2:\n\t" ASM_EXPTABLE(1b, 2b)
                             : "=b"(ret), "=c"(tmp)
                             : "m"(*(prtos_u8_t *)(unsigned long)addr));
        if (ret) return -1;
    }
    return 0;
}

#endif
#endif
#endif
