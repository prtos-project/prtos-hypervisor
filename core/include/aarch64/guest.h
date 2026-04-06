/*
 * FILE: guest.h
 *
 * AArch64 guest shared info
 *
 * http://www.prtos.org/
 */

#ifndef _PRTOS_ARCH_GUEST_H_
#define _PRTOS_ARCH_GUEST_H_

#include __PRTOS_INCFLD(arch/atomic.h)
#include __PRTOS_INCFLD(arch/processor.h)

/* This structure is visible from the guest (partition control table arch part) */
struct pct_arch {
    prtos_u64_t irq_saved_pc;       /* guest ELR saved before redirect */
    prtos_u64_t irq_saved_spsr;     /* guest SPSR saved before redirect */
    prtos_u64_t irq_saved_x0;      /* guest x0 saved before redirect */
    prtos_u32_t irq_vector;         /* vector number being delivered (0 = none) */
    prtos_u32_t _pad0;              /* alignment padding */
    prtos_u64_t trap_entry;         /* partition's trap dispatch stub address */
};

#endif
