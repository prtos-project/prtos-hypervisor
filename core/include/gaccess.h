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

/*
 * gp_to_va(ptr) - translate a guest parameter pointer to a hypervisor VA.
 * On x86 (VA==PA): identity.  On AArch64: IPA → Xen directmap EL2 VA.
 */
#define gp_to_va(ptr) __arch_gp_to_va(ptr)

#define PFLAG_RW 0x1
#define PFLAG_NOT_NULL 0x2

#ifdef CONFIG_MMU
static inline prtos_s32_t __check_gp_param_impl(void *param, prtos_u_size_t size, prtos_u32_t alignment, prtos_s32_t flags) {
    if ((flags & PFLAG_NOT_NULL) && !param) {
        return -1;
    }

    if (__arch_check_gp_param((prtos_address_t)param, size, alignment)) {
        return -1;
    }

    return ((flags & PFLAG_RW) ? asm_rw_check((prtos_address_t)param, size, alignment) : asm_ronly_check((prtos_address_t)param, size, alignment));
}

#ifdef CONFIG_AARCH64
/*
 * On AArch64, guest pointers are IPAs that must be translated to EL2 VAs.
 * This macro validates the pointer and then translates it in-place.
 */
#define check_gp_param(param, size, alignment, flags)                          \
    ({                                                                         \
        prtos_s32_t __ret = __check_gp_param_impl((void *)(param), size,       \
                                                   alignment, flags);          \
        if (__ret == 0 && (param)) {                                           \
            (param) = (typeof(param))gp_to_va((prtos_address_t)(param));       \
        }                                                                      \
        __ret;                                                                 \
    })
#else
#define check_gp_param(param, size, alignment, flags) \
    __check_gp_param_impl((void *)(param), size, alignment, flags)
#endif
#endif
#endif
