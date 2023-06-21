/*
 * FILE: lapic_timer.c
 *
 * Local & IO Advanced Programming Interrupts Controller (APIC)
 * IO-APIC 82093AA
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <stdc.h>
#include <kdevice.h>
#include <ktimer.h>
#include <virtmm.h>
#include <vmmap.h>
#include <arch/asm.h>
#include <arch/apic.h>
#include <arch/processor.h>
#include <arch/pic.h>
#include <arch/io.h>
#include <local.h>

RESERVE_HWIRQ(LAPIC_TIMER_IRQ);

static timer_handler_t lapic_handler[CONFIG_NO_CPUS];
static hw_timer_t lapic_current_timer[CONFIG_NO_CPUS];
static hw_time_t lapic_current_timer_hz[CONFIG_NO_CPUS];

#define CALIBRATE_TIME 10000
#define CALIBRATE_MULT 100
__VBOOT prtos_u32_t calibrate_lapic_current_timer(void) {
    prtos_u64_t start, stop;
    prtos_u32_t t1 = 0xffffffff, t2;

    stop = get_sys_clock_usec();
    while ((start = get_sys_clock_usec()) == stop)
        ;
    lapic_write(APIC_TMICT, t1);
    do {
        stop = get_sys_clock_usec();
    } while ((stop - start) < (CALIBRATE_TIME - 1));
    while (get_sys_clock_usec() == stop)
        ;
    t2 = lapic_read(APIC_TMCCT);

    return (t1 - t2) * CALIBRATE_MULT;
}

static void timer_irq_handler(cpu_ctxt_t *ctxt, void *irqData) {
    if (lapic_handler[GET_CPU_ID()]) (*lapic_handler[GET_CPU_ID()])();
}

static prtos_s32_t init_lapic_current_timer(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    // LAPIC Timer divisor set to 4
    lapic_write(APIC_TDCR, APIC_TDR_DIV_32);
    lapic_current_timer_hz[GET_CPU_ID()] = calibrate_lapic_current_timer();
    lapic_current_timer[GET_CPU_ID()].freq_khz = lapic_current_timer_hz[GET_CPU_ID()] / 1000;
    info->cpu.global_irq_mask &= ~(1 << LAPIC_TIMER_IRQ);
    set_irq_handler(LAPIC_TIMER_IRQ, timer_irq_handler, 0);
    lapic_write(APIC_LVTT, (LAPIC_TIMER_IRQ + FIRST_EXTERNAL_VECTOR) | APIC_LVT_MASKED);
    lapic_write(APIC_TMICT, 0);
    lapic_write(APIC_EOI, 0);
    hw_enable_irq(LAPIC_TIMER_IRQ);
    lapic_current_timer[GET_CPU_ID()].flags |= HWTIMER_ENABLED | HWTIMER_PER_CPU;

    return 1;
}

static void set_lapic_current_timer(prtos_time_t interval) {
    hw_time_t apic_tmict = (interval * lapic_current_timer_hz[GET_CPU_ID()]) / USECS_PER_SEC;

    hw_enable_irq(LAPIC_TIMER_IRQ);
    lapic_write(APIC_TMICT, (prtos_u32_t)apic_tmict);
}

static prtos_time_t get_max_interval_lapic(void) {
    return 1000000LL;  // 1s
}

static prtos_time_t get_min_interval_lapic(void) {
    return 50LL;  // 50usec
}

static timer_handler_t set_timer_handler_lapic(timer_handler_t timer_handler) {
    timer_handler_t old_lapic_user_handler = lapic_handler[GET_CPU_ID()];

    lapic_handler[GET_CPU_ID()] = timer_handler;

    return old_lapic_user_handler;
}

static void shutdown_lapic_current_timer(void) {
    lapic_current_timer[GET_CPU_ID()].flags &= ~HWTIMER_ENABLED;
}

static hw_timer_t lapic_current_timer[CONFIG_NO_CPUS] = {[0 ...(CONFIG_NO_CPUS - 1)] = {
                                                             .name = "lapic timer",
                                                             .flags = 0,
                                                             .init_hw_timer = init_lapic_current_timer,
                                                             .set_hw_timer = set_lapic_current_timer,
                                                             .get_max_interval = get_max_interval_lapic,
                                                             .get_min_interval = get_min_interval_lapic,
                                                             .set_timer_handler = set_timer_handler_lapic,
                                                             .shutdown_hw_timer = shutdown_lapic_current_timer,
                                                         }};

hw_timer_t *get_sys_hw_timer(void) {
    return &lapic_current_timer[GET_CPU_ID()];
}
