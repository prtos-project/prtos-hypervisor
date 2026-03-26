/******************************************************************************
 * preempt.h
 * 
 * Track atomic regions in the hypervisor which disallow sleeping.
 * 
 * Copyright (c) 2010, Keir Fraser <keir@xen.org>
 */

#ifndef __PRTOS_PREEMPT_H__
#define __PRTOS_PREEMPT_H__

#include <prtos_types.h>
#include <prtos_percpu.h>

DECLARE_PER_CPU(unsigned int, __preempt_count);

#define preempt_count() (this_cpu(__preempt_count))

#define preempt_disable() do {                  \
    preempt_count()++;                          \
    barrier();                                  \
} while (0)

#define preempt_enable() do {                   \
    barrier();                                  \
    preempt_count()--;                          \
} while (0)

bool in_atomic(void);

#ifndef NDEBUG
void ASSERT_NOT_IN_ATOMIC(void);
#else
#define ASSERT_NOT_IN_ATOMIC() ((void)0)
#endif

#endif /* __PRTOS_PREEMPT_H__ */
