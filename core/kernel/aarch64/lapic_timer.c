/*
 * FILE: armv8_timer.c
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

static timer_handler_t armv8_handler[CONFIG_NO_CPUS];
static hw_timer_t armv8_current_timer[CONFIG_NO_CPUS];
static hw_time_t armv8_current_timer_hz[CONFIG_NO_CPUS];

static void timer_irq_handler(cpu_ctxt_t *ctxt, void *irq_data) {
    if (armv8_handler[GET_CPU_ID()]) (*armv8_handler[GET_CPU_ID()])();
}

extern prtos_u32_t cpu_khz;
#define LAPIC_TIMER_IRQ 26

static prtos_s32_t init_armv8_current_timer(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    armv8_current_timer[GET_CPU_ID()].freq_khz = cpu_khz;
    prtos_s32_t e;
    for (e = 0; e < HWIRQS_VECTOR_SIZE; e++) {
        info->cpu.global_irq_mask[e] &= ~(1 << LAPIC_TIMER_IRQ);  // 26 is set for Hyper timer irq
        armv8_current_timer[GET_CPU_ID()].flags |= HWTIMER_PER_CPU;
    }

    set_irq_handler(LAPIC_TIMER_IRQ, timer_irq_handler, 0);
    hw_enable_irq(LAPIC_TIMER_IRQ);
    armv8_current_timer[GET_CPU_ID()].flags |= HWTIMER_ENABLED | HWTIMER_PER_CPU;

    return 1;
}

static void set_armv8_current_timer(prtos_time_t interval) {
    hw_time_t apic_tmict = (interval * armv8_current_timer_hz[GET_CPU_ID()]) / USECS_PER_SEC;
    hw_enable_irq(LAPIC_TIMER_IRQ);
}

static prtos_time_t get_max_interval_lapic(void) {
    return 1000000;  // 1s
}

static prtos_time_t get_min_interval_lapic(void) {
    return 50000;  // 50usec
}

static timer_handler_t set_timer_handler_lapic(timer_handler_t timer_handler) {
    timer_handler_t old_armv8_user_handler = armv8_handler[GET_CPU_ID()];

    armv8_handler[GET_CPU_ID()] = timer_handler;

    return old_armv8_user_handler;
}

static void shutdown_armv8_current_timer(void) {
    armv8_current_timer[GET_CPU_ID()].flags &= ~HWTIMER_ENABLED;
}

static hw_timer_t armv8_current_timer[CONFIG_NO_CPUS] = {[0 ...(CONFIG_NO_CPUS - 1)] = {
                                                             .name = "armv8 timer",
                                                             .flags = 0,
                                                             .init_hw_timer = init_armv8_current_timer,
                                                             .set_hw_timer = set_armv8_current_timer,
                                                             .get_max_interval = get_max_interval_lapic,
                                                             .get_min_interval = get_min_interval_lapic,
                                                             .set_timer_handler = set_timer_handler_lapic,
                                                             .shutdown_hw_timer = shutdown_armv8_current_timer,
                                                         }};

hw_timer_t *get_sys_hw_timer(void) {
    return &armv8_current_timer[GET_CPU_ID()];
}
