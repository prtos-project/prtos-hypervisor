/*
 * FILE: ktimer.h
 *
 * prtos's timer interface
 *
 * www.prtos.org
 *
 */

#ifndef _PRTOS_KTIMERS_H_
#define _PRTOS_KTIMERS_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#ifndef __ASSEMBLY__
#include <list.h>
#include <smp.h>

#define NSECS_PER_SEC 1000000000ULL
#define USECS_PER_SEC 1000000UL

typedef prtos_s32_t (*timer_handler_t)(void);

typedef struct hw_clock {
    char *name;
    prtos_u32_t flags;
#define HWCLOCK_ENABLED (1 << 0)
    prtos_u32_t freq_khz;
    prtos_s32_t (*init_clock)(void);
    prtos_time_t (*get_time_usec)(void);
    void (*shutdown_clock)(void);
} hw_clock_t;

extern hw_clock_t *sys_hw_clock;

// The following structure defines each hwtimer existing in the system
typedef struct hw_timer {
    prtos_s8_t *name;
    prtos_u32_t flags;
#define HWTIMER_ENABLED (1 << 0)
#define HWTIMER_PER_CPU (1 << 1)
    prtos_u32_t freq_khz;
    prtos_s32_t (*init_hw_timer)(void);
    void (*set_hw_timer)(prtos_time_t);
    // This is the maximum value to be programmed
    prtos_time_t (*get_max_interval)(void);
    prtos_time_t (*get_min_interval)(void);
    timer_handler_t (*set_timer_handler)(timer_handler_t);
    void (*shutdown_hw_timer)(void);
} hw_timer_t;

extern hw_timer_t *sys_hw_timer;

typedef struct ktimer {
    struct dyn_list_node dyn_list_ptrs;  // hard-coded, don't touch
    hw_time_t value;
    hw_time_t interval;
    prtos_u32_t flags;
#define KTIMER_ARMED (1 << 0)
    void *actionArgs;
    void (*Action)(struct ktimer *, void *);
} ktimer_t;

typedef struct {
    prtos_time_t value;
    prtos_time_t interval;
#define VTIMER_ARMED (1 << 0)
    prtos_u32_t flags;
    ktimer_t ktimer;
} vtimer_t;

typedef struct {
    prtos_time_t acc;
    prtos_time_t delta;
    prtos_u32_t flags;
#define VCLOCK_ENABLED (1 << 0)
} vclock_t;

extern prtos_s32_t setup_ktimers(void);
extern void setup_sys_clock(void);
extern void setup_hw_timer(void);
extern hw_timer_t *get_sys_hw_timer(void);

static inline prtos_time_t get_sys_clock_usec(void) {
    return sys_hw_clock->get_time_usec();
}

static inline prtos_time_t get_time_usec_vclock(vclock_t *vclock) {
    prtos_time_t t = vclock->acc;
    if (vclock->flags & VCLOCK_ENABLED) t += (get_sys_clock_usec() - vclock->delta);

    return t;
}

extern void init_ktimer(int cpu_id, ktimer_t *ktimer, void (*Act)(ktimer_t *, void *), void *args, void *kthread);
extern void uninit_ktimer(ktimer_t *ktimer, void *kthread);
extern prtos_s32_t arm_ktimer(ktimer_t *ktimer, prtos_time_t value, prtos_time_t interval);
extern prtos_s32_t disarm_ktimer(ktimer_t *ktimer);
extern prtos_s32_t init_vtimer(int cpu_id, vtimer_t *vtimer, void *k);
extern prtos_s32_t arm_vtimer(vtimer_t *vtimer, vclock_t *vclock, prtos_time_t value, prtos_time_t interval);
extern prtos_s32_t disarm_vtimer(vtimer_t *vtimer, vclock_t *vclock);
extern inline void set_hw_timer(prtos_time_t next_act);
extern prtos_time_t traverse_ktimer_queue(struct dyn_list *l, prtos_time_t current_time);

static inline void init_vclock(vclock_t *vclock) {
    vclock->acc = 0;
    vclock->delta = 0;
    vclock->flags = 0;
}

static inline void stop_vclock(vclock_t *vclock, vtimer_t *vtimer) {
    if (vtimer->flags & VTIMER_ARMED) disarm_ktimer(&vtimer->ktimer);

    vclock->flags &= (~VCLOCK_ENABLED);
    vclock->acc += get_sys_clock_usec() - vclock->delta;
}

static inline void resume_vclock(vclock_t *vclock, vtimer_t *vtimer) {
    vclock->delta = get_sys_clock_usec();
    vclock->flags |= VCLOCK_ENABLED;

    if (vtimer->flags & VTIMER_ARMED) arm_ktimer(&vtimer->ktimer, vtimer->value - vclock->acc + vclock->delta, vtimer->interval);
}

static inline prtos_time_t hwtime_to_duration(hw_time_t t, hw_time_t hz) {
    hw_time_t s;
    s = t / hz;
    return (s * USECS_PER_SEC + ((t - s * hz) * USECS_PER_SEC) / hz);
}

static inline hw_time_t duration_to_hwtime(prtos_time_t d, hw_time_t hz) {
    hw_time_t s;
    s = d / USECS_PER_SEC;
    return s * hz + ((d - s * USECS_PER_SEC) * hz) / USECS_PER_SEC;
}

#endif
#endif
