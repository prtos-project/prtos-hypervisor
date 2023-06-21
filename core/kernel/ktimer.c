/*
 * FILE: ktimers.c
 *
 * prtos's timer interface
 *
 * www.prtos.com
 *
 */

#include <assert.h>
#include <rsvmem.h>
#include <boot.h>
#include <ktimer.h>
#include <sched.h>
#include <spinlock.h>
#include <smp.h>
#include <stdc.h>
#include <local.h>

static prtos_s32_t timer_handler(void);

inline void set_hw_timer(prtos_time_t next_act) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_time_t next_time, current_time;

    ASSERT(!hw_is_sti());
    ASSERT(next_act >= 0);
    if (!next_act) return;
    if ((info->time.flags & NEXT_ACT_IS_VALID) && (next_act >= info->time.next_act)) return;
    info->time.flags |= NEXT_ACT_IS_VALID;
    info->time.next_act = next_act;
    current_time = get_sys_clock_usec();
    next_time = next_act - current_time;

    if (next_time >= 0) {
        // ASSERT(next_time>0);
        if (next_time < info->time.sys_hw_timer->get_min_interval()) next_time = info->time.sys_hw_timer->get_min_interval();

        if (next_time > info->time.sys_hw_timer->get_max_interval()) next_time = info->time.sys_hw_timer->get_max_interval();
        info->time.next_act = next_time + current_time;
        info->time.sys_hw_timer->set_hw_timer(next_time);
    } else
        timer_handler();
}

prtos_time_t traverse_ktimer_queue(struct dyn_list *l, prtos_time_t current_time) {
    prtos_time_t next_act = MAX_PRTOSTIME;
    ktimer_t *ktimer;

    DYNLIST_FOR_EACH_ELEMENT_BEGIN(l, ktimer, 1) {
        ASSERT(ktimer);
        if (ktimer->flags & KTIMER_ARMED) {
            if (ktimer->value <= current_time) {
                if (ktimer->Action) ktimer->Action(ktimer, ktimer->actionArgs);

                if (ktimer->interval > 0) {
                    // To be optimised
                    do {
                        ktimer->value += ktimer->interval;
                    } while (ktimer->value <= current_time);
                    if (next_act > ktimer->value) next_act = ktimer->value;
                } else
                    ktimer->flags &= ~KTIMER_ARMED;
            } else {
                if (next_act > ktimer->value) next_act = ktimer->value;
            }
        }
    }
    DYNLIST_FOR_EACH_ELEMENT_END(l);

    return next_act;
}

static prtos_s32_t timer_handler(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_time_t current_time, next_act, nLocalAct;

    ASSERT(!hw_is_sti());
    info->time.flags &= ~NEXT_ACT_IS_VALID;
    current_time = get_sys_clock_usec();
    next_act = traverse_ktimer_queue(&info->time.global_active_ktimers, current_time);
    if (info->sched.current_kthread->ctrl.g)
        if ((nLocalAct = traverse_ktimer_queue(&info->sched.current_kthread->ctrl.local_active_ktimers, current_time)) && (nLocalAct < next_act))
            next_act = nLocalAct;
    set_hw_timer(next_act);

    return 0;
}

void init_ktimer(int cpu_id, ktimer_t *ktimer, void (*Act)(ktimer_t *, void *), void *args, void *kthread) {
    kthread_t *k = (kthread_t *)kthread;

    memset((prtos_s8_t *)ktimer, 0, sizeof(ktimer_t));
    ktimer->actionArgs = args;
    ktimer->Action = Act;
    if (dyn_list_insert_head((k) ? &k->ctrl.local_active_ktimers : &local_processor_info[cpu_id].time.global_active_ktimers, &ktimer->dyn_list_ptrs)) {
        cpu_ctxt_t ctxt;
        get_cpu_ctxt(&ctxt);
        system_panic(&ctxt, "[KTIMER] Error allocating ktimer");
    }
}

void uninit_ktimer(ktimer_t *ktimer, void *kthread) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = (kthread_t *)kthread;

    ktimer->flags = 0;
    if (dyn_list_remove_element((k) ? &k->ctrl.local_active_ktimers : &info->time.global_active_ktimers, &ktimer->dyn_list_ptrs)) {
        cpu_ctxt_t ctxt;
        get_cpu_ctxt(&ctxt);
        system_panic(&ctxt, "[KTIMER] Error freeing ktimer");
    }
}

prtos_s32_t arm_ktimer(ktimer_t *ktimer, prtos_time_t value, prtos_time_t interval) {
    ASSERT(!hw_is_sti());
    ASSERT(ktimer);
    ktimer->value = value;
    ktimer->interval = interval;
    ktimer->flags |= KTIMER_ARMED;
    set_hw_timer(value);

    return 0;
}

prtos_s32_t disarm_ktimer(ktimer_t *ktimer) {
    ASSERT(ktimer);
    if (!(ktimer->flags & KTIMER_ARMED)) return -1;
    ktimer->flags &= ~VTIMER_ARMED;
    return 0;
}

static void vtimer_handle(ktimer_t *ktimer, void *args) {
    kthread_t *k = (kthread_t *)args;
    ASSERT(k->ctrl.g->vclock.flags & VCLOCK_ENABLED);
    ASSERT(k->ctrl.g->vtimer.flags & VTIMER_ARMED);
    CHECK_KTHR_SANITY(k);
    if (k->ctrl.g->vtimer.interval > 0)
        k->ctrl.g->vtimer.value += k->ctrl.g->vtimer.interval;
    else
        k->ctrl.g->vtimer.flags &= ~VTIMER_ARMED;

    set_ext_irq_pending(k, PRTOS_VT_EXT_EXEC_TIMER);
}

prtos_s32_t init_vtimer(int cpu_id, vtimer_t *vtimer, void *k) {
    kthread_t *scK;

    scK = (prtos_conf_table.hpv.cpu_table[cpu_id].sched_policy == CYCLIC_SCHED) ? k : 0;
    init_ktimer(cpu_id, &vtimer->ktimer, vtimer_handle, k, scK);
    return 0;
}

prtos_s32_t arm_vtimer(vtimer_t *vtimer, vclock_t *vclock, prtos_time_t value, prtos_time_t interval) {
    vtimer->value = value;

    vtimer->interval = interval;
    vtimer->flags |= VTIMER_ARMED;
    if (vclock->flags & VCLOCK_ENABLED) arm_ktimer(&vtimer->ktimer, value - vclock->acc + vclock->delta, interval);

    return 0;
}

prtos_s32_t disarm_vtimer(vtimer_t *vtimer, vclock_t *vclock) {
    if (!(vtimer->flags & KTIMER_ARMED)) return -1;
    vtimer->flags &= ~KTIMER_ARMED;
    return 0;
}

// Busy wait
prtos_s32_t u_delay(prtos_u32_t usec) {
    prtos_time_t waitUntil = (prtos_time_t)usec + get_sys_clock_usec();

    while (waitUntil > get_sys_clock_usec())
        ;
    return 0;
}

prtos_s32_t __VBOOT setup_ktimers(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    dyn_list_init(&info->time.global_active_ktimers);
    info->time.sys_hw_timer->set_timer_handler(timer_handler);
    return 0;
}

void __VBOOT setup_sys_clock(void) {
    if (!sys_hw_clock || (sys_hw_clock->init_clock() < 0)) {
        cpu_ctxt_t ctxt;
        get_cpu_ctxt(&ctxt);
        system_panic(&ctxt, "No system clock available\n");
    }

    kprintf(">> HWClocks [%s (%dKhz)]\n", sys_hw_clock->name, sys_hw_clock->freq_khz);
}

void __VBOOT setup_hw_timer(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    if (!(info->time.sys_hw_timer = get_sys_hw_timer()) || (info->time.sys_hw_timer->init_hw_timer() < 0)) {
        cpu_ctxt_t ctxt;
        get_cpu_ctxt(&ctxt);
        system_panic(&ctxt, "No hw_timer available\n");
    }

    if ((GET_NRCPUS() > 1) && !(info->time.sys_hw_timer->flags & HWTIMER_PER_CPU)) {
        cpu_ctxt_t ctxt;
        get_cpu_ctxt(&ctxt);
        system_panic(&ctxt, "No hw_timer available\n");
    }

    kprintf("[CPU%d] >> HwTimer [%s (%dKhz)]\n", GET_CPU_ID(), info->time.sys_hw_timer->name, info->time.sys_hw_timer->freq_khz);
}
