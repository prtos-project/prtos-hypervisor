/*
 * FILE: guest.h
 *
 * RISC-V 64-bit guest shared info
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_GUEST_H_
#define _PRTOS_ARCH_GUEST_H_

#include __PRTOS_INCFLD(arch/atomic.h)
#include __PRTOS_INCFLD(arch/processor.h)

/* This structure is visible from the guest (partition control table arch part) */
struct pct_arch {
    prtos_u64_t irq_saved_pc;     /* guest sepc saved before redirect */
    prtos_u64_t irq_saved_sstatus; /* guest sstatus saved before redirect */
    prtos_u64_t irq_saved_a0;    /* guest a0 saved before redirect */
    prtos_u32_t irq_vector;       /* vector number being delivered (0 = none) */
    prtos_u32_t _pad0;            /* alignment padding */
    prtos_u64_t trap_entry;       /* partition's trap dispatch stub address */
};

#endif
