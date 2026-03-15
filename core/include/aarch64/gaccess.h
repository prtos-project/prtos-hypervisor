/*
 * FILE: gaccess.h
 *
 * Guest shared info
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_GACCESS_H_
#define _PRTOS_ARCH_GACCESS_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <arch/prtos_def.h>
#include <arch/asm.h>

#define __arch_g_param
#define __arch_check_gp_param(__param, __size, __align)                                     \
    ({                                                                                      \
        prtos_s32_t __r = -1;                                                               \
        if (__param < CONFIG_PRTOS_OFFSET && __param + size < CONFIG_PRTOS_OFFSET) __r = 0; \
        __r;                                                                                \
    })

/*
 * Translate a partition IPA (guest physical / virtual address) to an EL2
 * hypervisor virtual address via Xen's directmap.
 * Defined in core/kernel/aarch64/mmu.c.
 */
extern void *prtos_ipa_to_va(prtos_u64_t ipa);
#define __arch_gp_to_va(ptr) prtos_ipa_to_va((prtos_u64_t)(prtos_address_t)(ptr))

#endif
