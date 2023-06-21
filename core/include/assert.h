/*
 * FILE: assert.h
 *
 * Assert definition
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ASSERT_H_
#define _PRTOS_ASSERT_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <processor.h>
#include <spinlock.h>

#ifdef CONFIG_DEBUG

#define ASSERT(exp)                                                                              \
    do {                                                                                         \
        if (!(exp)) {                                                                            \
            cpu_ctxt_t _ctxt;                                                                    \
            get_cpu_ctxt(&_ctxt);                                                                \
            system_panic(&_ctxt, __PRTOS_FILE__ ":%u: failed assertion `" #exp "'\n", __LINE__); \
        }                                                                                        \
        ((void)0);                                                                               \
    } while (0)

#define ASSERT_LOCK(exp, lock)                                                                   \
    do {                                                                                         \
        if (!(exp)) {                                                                            \
            cpu_ctxt_t _ctxt;                                                                    \
            spin_unlock((lock));                                                                 \
            get_cpu_ctxt(&_ctxt);                                                                \
            system_panic(&_ctxt, __PRTOS_FILE__ ":%u: failed assertion `" #exp "'\n", __LINE__); \
        }                                                                                        \
        ((void)0);                                                                               \
    } while (0)

#else

#define ASSERT(exp) ((void)0)
#define ASSERT_LOCK(exp, sLock) ((void)0)
#endif

#endif
