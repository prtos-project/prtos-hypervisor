/*
 * FILE: irqs.h
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
    prtos_u64_t x0, x1, x2, x3, x4, x5, x6, x7;
    prtos_u64_t x8, x9, x10, x11, x12, x13, x14, x15;
    prtos_u64_t x16, x17, x18;
    prtos_u64_t fp;       /* x29 */
    prtos_u64_t lr;       /* x30 */
    prtos_u64_t irq_nr;   /* vector number */
} trap_ctxt_t;

#endif /*__ASSEMBLY__*/
#endif /*_BAIL_ARCH_IRQS_H_*/
