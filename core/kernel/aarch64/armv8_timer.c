/*
 * FILE: armv8_timer.c
 *
 * ARMv8 system clock and timer implementation for PRTOS.
 * Provides sys_hw_clock and get_sys_hw_timer() used by core/kernel/ktimer.c.
 *
 * http://www.prtos.org/
 */

#include <assert.h>
#include <boot.h>
#include <stdc.h>
#include <kdevice.h>
#include <ktimer.h>
#include <virtmm.h>
#include <vmmap.h>
#include <arch/asm.h>
#include <arch/processor.h>
#include <local.h>

/* ---- ARMv8 system clock (from generic timer) ---- */

static prtos_u64_t prtos_boot_count;
static hw_clock_t armv8_clock;

extern prtos_u64_t prtos_get_current_circle(void);
extern prtos_s64_t prtos_get_s_time(prtos_u64_t prtos_boot_count);

static prtos_s32_t init_armv8_clock(void) {
    armv8_clock.flags |= HWCLOCK_ENABLED;
    prtos_boot_count = prtos_get_current_circle();
    return 1;
}

static prtos_time_t read_armv8_clock_usec(void) {
    return prtos_get_s_time(prtos_boot_count) / 1000;
}

static hw_clock_t armv8_clock = {
    .name = "ARMv8 clock",
    .flags = 0,
    .freq_khz = 0,
    .init_clock = init_armv8_clock,
    .get_time_usec = read_armv8_clock_usec,
    .shutdown_clock = 0,
};

hw_clock_t *sys_hw_clock = &armv8_clock;

unsigned int get_frequency_khz_prtos(void);
__VBOOT void init_hpet(void) {
    armv8_clock.freq_khz = get_frequency_khz_prtos();
}

/* ---- ARMv8 per-CPU hardware timer ---- */

#define ARMV8_TIMER_IRQ 26

static timer_handler_t armv8_handler[CONFIG_NO_CPUS];
static hw_timer_t armv8_current_timer[CONFIG_NO_CPUS];

static void timer_irq_handler(cpu_ctxt_t *ctxt, void *irq_data) {
    if (armv8_handler[GET_CPU_ID()])
        (*armv8_handler[GET_CPU_ID()])();
}

extern prtos_u32_t cpu_khz;

static prtos_s32_t init_armv8_current_timer(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    armv8_current_timer[GET_CPU_ID()].freq_khz = cpu_khz;
    prtos_s32_t e;
    for (e = 0; e < HWIRQS_VECTOR_SIZE; e++) {
        info->cpu.global_irq_mask[e] &= ~(1 << ARMV8_TIMER_IRQ);
        armv8_current_timer[GET_CPU_ID()].flags |= HWTIMER_PER_CPU;
    }

    set_irq_handler(ARMV8_TIMER_IRQ, timer_irq_handler, 0);
    hw_enable_irq(ARMV8_TIMER_IRQ);
    armv8_current_timer[GET_CPU_ID()].flags |= HWTIMER_ENABLED | HWTIMER_PER_CPU;

    return 1;
}

extern int reprogram_timer(long long timeout);
extern long long get_s_time(void);

static void set_armv8_current_timer_interval(prtos_time_t interval) {
    long long deadline_ns = get_s_time() + (long long)interval * 1000LL;
    reprogram_timer(deadline_ns);
    hw_enable_irq(ARMV8_TIMER_IRQ);
}

static prtos_time_t get_max_interval(void) {
    return 1000000;  /* 1s */
}

static prtos_time_t get_min_interval(void) {
    return 50000;  /* 50us */
}

static timer_handler_t set_timer_handler(timer_handler_t timer_handler) {
    timer_handler_t old = armv8_handler[GET_CPU_ID()];
    armv8_handler[GET_CPU_ID()] = timer_handler;
    return old;
}

static void shutdown_armv8_current_timer(void) {
    armv8_current_timer[GET_CPU_ID()].flags &= ~HWTIMER_ENABLED;
}

static hw_timer_t armv8_current_timer[CONFIG_NO_CPUS] = {
    [0 ... (CONFIG_NO_CPUS - 1)] = {
        .name = "armv8 timer",
        .flags = 0,
        .init_hw_timer = init_armv8_current_timer,
        .set_hw_timer = set_armv8_current_timer_interval,
        .get_max_interval = get_max_interval,
        .get_min_interval = get_min_interval,
        .set_timer_handler = set_timer_handler,
        .shutdown_hw_timer = shutdown_armv8_current_timer,
    }
};

hw_timer_t *get_sys_hw_timer(void) {
    return &armv8_current_timer[GET_CPU_ID()];
}
