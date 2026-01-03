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
#define __arch_check_gp_param(__param, __size, __align)                                      \
    ({                                                                                      \
        prtos_s32_t __r = -1;                                                               \
        if (__param < CONFIG_PRTOS_OFFSET && __param + size < CONFIG_PRTOS_OFFSET) __r = 0; \
        __r;                                                                                \
    })

#endif
