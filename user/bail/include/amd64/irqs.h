/*
 * FILE: irqs.h
 *
 * www.prtos.org
 */

#ifndef _BAIL_ARCH_IRQS_H_
#define _BAIL_ARCH_IRQS_H_

#define TRAPTAB_LENGTH 256

#ifndef __ASSEMBLY__

#define hw_cli() prtos_x86_clear_if()
#define hw_sti() prtos_x86_set_if()
#define hw_is_sti() ((prtos_get_pct()->iflags) & _CPU_FLAG_IF)

typedef struct trap_ctxt_t {
    prtos_u64_t r15;
    prtos_u64_t r14;
    prtos_u64_t r13;
    prtos_u64_t r12;
    prtos_u64_t r11;
    prtos_u64_t r10;
    prtos_u64_t r9;
    prtos_u64_t r8;
    prtos_u64_t rbx;
    prtos_u64_t rcx;
    prtos_u64_t rdx;
    prtos_u64_t rsi;
    prtos_u64_t rdi;
    prtos_u64_t rbp;
    prtos_u64_t rax;

    prtos_u64_t irq_nr;
    prtos_u64_t err_code;

    prtos_u64_t ip;
    prtos_u64_t cs;
    prtos_u64_t flags;
    prtos_u64_t sp;
    prtos_u64_t ss;
} trap_ctxt_t;

#endif /*__ASSEMBLY__*/
#endif /*_BAIL_ARCH_IRQS_H_*/
