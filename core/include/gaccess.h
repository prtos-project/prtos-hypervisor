/*
 * FILE: gaccess.h
 *
 * Access rules from PRTOS to partition address space
 *
 * www.prtos.org
 */

#ifndef _PRTOS_GACCESS_H_
#define _PRTOS_GACCESS_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <arch/gaccess.h>

#define __g_param __arch_g_param

#define PFLAG_RW 0x1
#define PFLAG_NOT_NULL 0x2

#ifdef CONFIG_MMU
static inline prtos_s32_t check_gp_param(void *param, prtos_u_size_t size, prtos_u32_t alignment, prtos_s32_t flags) {
    if ((flags & PFLAG_NOT_NULL) && !param) {
        return -1;
    }

    if (__arch_check_gp_param((prtos_address_t)param, size, alignment)) {
        return -1;
    }

    return ((flags & PFLAG_RW) ? asm_rw_check((prtos_address_t)param, size, alignment) : asm_ronly_check((prtos_address_t)param, size, alignment));
}
#endif
#endif
