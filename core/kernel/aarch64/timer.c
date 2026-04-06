/*
 * FILE: timer.c
 *
 * AArch64 timer driver (ARM Generic Timer at EL2)
 *
 * http://www.prtos.org/
 */

#include <kthread.h>
#include <ktimer.h>
#include <stdc.h>
#include <processor.h>

static inline prtos_u64_t read_cntpct(void) {
    prtos_u64_t val;
    __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

static inline prtos_u64_t read_cntfrq(void) {
    prtos_u64_t val;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static prtos_u64_t timer_freq;

static prtos_time_t aarch64_get_time_usec(void) {
    prtos_u64_t t = read_cntpct();
    /* Convert counter ticks to microseconds */
    return (prtos_time_t)(t / (timer_freq / 1000000));
}

static void aarch64_set_hw_timer(prtos_time_t delta) {
    prtos_u64_t ticks = delta * (timer_freq / 1000000);
    if (ticks < 100) ticks = 100;
    /* Use CNTHP (EL2 physical timer) */
    __asm__ __volatile__(
        "msr cnthp_tval_el2, %0\n\t"
        "mov x0, #1\n\t"
        "msr cnthp_ctl_el2, x0\n\t"
        "isb\n\t"
        : : "r"(ticks) : "x0", "memory"
    );
}

static prtos_time_t aarch64_get_max_interval(void) {
    return 1000000LL;  /* 1 second in usec */
}

static prtos_time_t aarch64_get_min_interval(void) {
    return 10;  /* 10us minimum */
}

static prtos_s32_t aarch64_init_hw_timer(void) {
    /* Enable CNTHP timer, unmask interrupt */
    __asm__ __volatile__(
        "mov x0, #1\n\t"
        "msr cnthp_ctl_el2, x0\n\t"
        "isb\n\t"
        : : : "x0", "memory"
    );
    return 0;
}

/* Timer handler registered by the kernel timer subsystem */
timer_handler_t aarch64_timer_handler;

static timer_handler_t aarch64_set_timer_handler(timer_handler_t handler) {
    timer_handler_t old = aarch64_timer_handler;
    aarch64_timer_handler = handler;
    return old;
}

static hw_timer_t aarch64_timer = {
    .name = "aarch64-timer",
    .flags = HWTIMER_ENABLED | HWTIMER_PER_CPU,
    .freq_khz = 0,  /* Set at runtime from CNTFRQ */
    .init_hw_timer = aarch64_init_hw_timer,
    .set_hw_timer = aarch64_set_hw_timer,
    .get_max_interval = aarch64_get_max_interval,
    .get_min_interval = aarch64_get_min_interval,
    .set_timer_handler = aarch64_set_timer_handler,
};

hw_timer_t *get_sys_hw_timer(void) {
    return &aarch64_timer;
}

static prtos_s32_t aarch64_init_clock(void) {
    return 0;
}

static hw_clock_t aarch64_clock = {
    .name = "aarch64-clock",
    .flags = HWCLOCK_ENABLED,
    .freq_khz = 0,  /* Set at runtime */
    .init_clock = aarch64_init_clock,
    .get_time_usec = aarch64_get_time_usec,
};

hw_clock_t *sys_hw_clock = &aarch64_clock;

/* init_hpet - compatibility name used by common setup code */
void init_hpet(void) {
}

void init_aarch64_timer(void) {
    timer_freq = read_cntfrq();
    aarch64_timer.freq_khz = (prtos_u32_t)(timer_freq / 1000);
    aarch64_clock.freq_khz = (prtos_u32_t)(timer_freq / 1000);

    /* Enable CNTHP (EL2 physical timer) */
    __asm__ __volatile__(
        "mov x0, #1\n\t"
        "msr cnthp_ctl_el2, x0\n\t"
        "isb\n\t"
        : : : "x0", "memory"
    );
}

/* cpu_khz defined in common setup.c */
