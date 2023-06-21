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
    prtos_u32_t ebx;
    prtos_u32_t ecx;
    prtos_u32_t edx;
    prtos_u32_t esi;
    prtos_u32_t edi;
    prtos_u32_t ebp;
    prtos_u32_t eax;
    prtos_u32_t ds;
    prtos_u32_t es;
    prtos_u32_t fs;
    prtos_u32_t gs;

    prtos_u32_t irq_nr;
    prtos_u32_t err_code;

    prtos_u32_t ip;
    prtos_u32_t cs;
    prtos_u32_t flags;
    prtos_u32_t sp;
    prtos_u32_t ss;
} trap_ctxt_t;

#endif /*__ASSEMBLY__*/
#endif /*_BAIL_ARCH_IRQS_H_*/
