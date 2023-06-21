/*
 * FILE: setup.c
 *
 * Setting up and starting up the kernel
 *
 * www.prtos.org
 */

#include <assert.h>
#include <stdc.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <kthread.h>
#include <vmmap.h>
#include <virtmm.h>

__NOINLINE void free_boot_mem(void) {
    extern barrier_t smp_start_barrier;
    extern void idle_task(void);
    ASSERT(!hw_is_sti());
    barrier_unlock(&smp_start_barrier);
    GET_LOCAL_PROCESSOR()->sched.flags |= LOCAL_SCHED_ENABLED;
    schedule();
    idle_task();
}
