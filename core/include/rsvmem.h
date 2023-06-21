/*
 * FILE: rsvmem.h
 *
 * Memory for structure initialisation
 *
 * www.prtos.org
 */

#ifndef _PRTOS_RSVMEM_H_
#define _PRTOS_RSVMEM_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <stdc.h>
#include <arch/prtos_def.h>

extern void init_rsv_mem(void);
extern void *alloc_rsv_mem(prtos_u32_t size, prtos_u32_t align);
#ifdef CONFIG_DEBUG
extern void rsv_mem_debug(void);
#endif

#define GET_MEMA(c, s, a)                                                                    \
    do {                                                                                     \
        if (s) {                                                                             \
            if (!(c = alloc_rsv_mem(s, a))) {                                                \
                cpu_ctxt_t _ctxt;                                                            \
                get_cpu_ctxt(&_ctxt);                                                        \
                system_panic(&_ctxt, __PRTOS_FILE__ ":%u: memory pool exhausted", __LINE__); \
            }                                                                                \
        } else                                                                               \
            c = 0;                                                                           \
    } while (0)

#define GET_MEM(c, s) GET_MEMA(c, s, ALIGNMENT)

#define GET_MEMAZ(c, s, a)                                                                   \
    do {                                                                                     \
        if (s) {                                                                             \
            if (!(c = alloc_rsv_mem(s, a))) {                                                \
                cpu_ctxt_t _ctxt;                                                            \
                get_cpu_ctxt(&_ctxt);                                                        \
                system_panic(&_ctxt, __PRTOS_FILE__ ":%u: memory pool exhausted", __LINE__); \
            }                                                                                \
            memset(c, 0, s);                                                                 \
        } else                                                                               \
            c = 0;                                                                           \
    } while (0)

#define GET_MEMZ(c, s) GET_MEMAZ(c, s, ALIGNMENT)

#endif
