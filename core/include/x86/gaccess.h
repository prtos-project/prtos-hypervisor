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
#define __arch_check_gp_aram(__param, __size, __align) ({ \
    prtos_s32_t __r=-1; \
    if (!((__align - 1) & __param)) \
        if ((prtos_address_t)__param < CONFIG_PRTOS_OFFSET) \
            if (CONFIG_PRTOS_OFFSET-(prtos_address_t)(__param) >= size) \
                __r=0; \
    __r; \
)}

#endif
