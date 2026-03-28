/*
 * FILE: irqs.h
 *
 * RISC-V 64 BAIL interrupt/trap definitions
 *
 * www.prtos.org
 */

#ifndef _BAIL_ARCH_IRQS_H_
#define _BAIL_ARCH_IRQS_H_

#define TRAPTAB_LENGTH 256

#ifndef __ASSEMBLY__

#define hw_cli() do {} while(0)
#define hw_sti() do {} while(0)
#define hw_is_sti() 1

typedef struct trap_ctxt_t {
    prtos_u64_t a0, a1, a2, a3, a4, a5, a6, a7;
    prtos_u64_t t0, t1, t2, t3, t4, t5, t6;
    prtos_u64_t ra;
    prtos_u64_t fp;       /* s0 */
    prtos_u64_t irq_nr;   /* vector number */
} trap_ctxt_t;

#endif /*__ASSEMBLY__*/
#endif /*_BAIL_ARCH_IRQS_H_*/
