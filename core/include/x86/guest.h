/*
 * FILE: guest.h
 *
 * Guest shared info
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_GUEST_H_
#define _PRTOS_ARCH_GUEST_H_

#include __PRTOS_INCFLD(arch/atomic.h)
#include __PRTOS_INCFLD(arch/processor.h)

// this structure is visible from the guest
struct pct_arch {
    struct x86_desc_reg gdtr;
    struct x86_desc_reg idtr;
    prtos_u32_t max_idt_vec;
    volatile prtos_u32_t tr;
    volatile prtos_u32_t cr4;
    volatile prtos_u32_t cr3;
#define _ARCH_PTDL1_REG cr3
    volatile prtos_u32_t cr2;
    volatile prtos_u32_t cr0;
};

#endif
