/*
 * FILE: setup.c
 *
 * Setting up and starting up the kernel
 *
 * http://www.prtos.org/
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
#ifdef CONFIG_loongarch64
    {
        kthread_t *idle = GET_LOCAL_PROCESSOR()->sched.idle_kthread;
        prtos_address_t new_sp = (prtos_address_t)&idle->kstack[CONFIG_KSTACK_SIZE];
        extern void _enter_idle_on_own_stack(prtos_address_t sp) __attribute__((noreturn));
        _enter_idle_on_own_stack(new_sp);
    }
#endif
    schedule();
    idle_task();
}
