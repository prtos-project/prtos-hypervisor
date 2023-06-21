/*
 * FILE: pit.c
 *
 * pit driver
 *
 * www.prtos.org
 *
 */
#ifdef CONFIG_PC_PIT_TIMER
#include <assert.h>
#include <boot.h>
#include <kdevice.h>
#include <ktimer.h>
#include <smp.h>
#include <stdc.h>
#include <processor.h>
#include <arch/io.h>
#include <local.h>

// Definitions
#define PIT_IRQ_NR 0
#define PIT_HZ 1193182UL
#define PIT_KHZ 1193UL
#define PIT_ACCURATELY 60
#define PIT_MODE 0x43
#define PIT_CH0 0x40
#define PIT_CH2 0x42

// read-back mode constants
#define PIT_LATCH_CNT0 0xD2
#define PIT_LATCH_CNT2 0xD8
#define PIT_LATCH_CNT0_2 0xDA

RESERVE_HWIRQ(PIT_IRQ_NR);
RESERVE_IOPORTS(0x40, 4);

#ifdef CONFIG_PC_PIT_CLOCK
#define PIT_PERIOD_USEC 1000

// Init count value for 1 mill second
#define PIT_PERIOD ((PIT_PERIOD_USEC * PIT_HZ) / USECS_PER_SEC)
static struct pit_clock_data { volatile prtos_u32_t ticks; } pit_clock_data;
#endif

static hw_timer_t pit_timer;
static timer_handler_t pit_handler;

static inline void set_pit_timer_hw_time(prtos_u16_t pitCounter) {
#ifndef CONFIG_PC_PIT_CLOCK
    // ONESHOOT_MODE
    out_byte_port(0x30, PIT_MODE);  // binary, mode 0, LSB/MSB, ch 0 */
    out_byte_port(pitCounter & 0xff, PIT_CH0);
    out_byte_port(pitCounter >> 8, PIT_CH0);
#endif
}

static void timer_irq_handler(cpu_ctxt_t *ctxt, void *irqData) {
#ifdef CONFIG_PC_PIT_CLOCK
    pit_clock_data.ticks++;
#endif
    if (pit_handler) (*pit_handler)();
    hw_enable_irq(PIT_IRQ_NR);
}

static prtos_s32_t init_pit_timer(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    info->cpu.global_irq_mask &= ~(1 << PIT_IRQ_NR);
    set_irq_handler(PIT_IRQ_NR, timer_irq_handler, 0);
#ifdef CONFIG_PC_PIT_CLOCK
    out_byte_port(0x34, PIT_MODE);  // binary, mode 2, LSB/MSB, ch 0
    out_byte_port(PIT_PERIOD & 0xff, PIT_CH0);
    out_byte_port(PIT_PERIOD >> 8, PIT_CH0);
#else
    // setting counter 0 in oneshot mode
    out_byte_port(0x30, PIT_MODE);  // binary, mode 0, LSB/MSB, ch 0 */
#endif
    pit_timer.flags |= HWTIMER_ENABLED;
    hw_enable_irq(PIT_IRQ_NR);

    return 1;
}

static void set_pit_timer(prtos_time_t interval) {
    prtos_u16_t pitCounter = (interval * PIT_HZ) / USECS_PER_SEC;
    set_pit_timer_hw_time(pitCounter);
}

static prtos_time_t get_pit_timer_max_interval(void) {
    return 1000000;  //(0xF0*USECS_PER_SEC)/PIT_HZ;
}

static prtos_time_t get_pit_timer_min_interval(void) {
    return PIT_ACCURATELY;
}

static timer_handler_t set_pit_timer_handler(timer_handler_t timer_handler) {
    timer_handler_t OldPitUserHandler = pit_handler;
    pit_handler = timer_handler;
    return OldPitUserHandler;
}

static void pit_timer_shutdown(void) {
    pit_timer.flags &= ~HWTIMER_ENABLED;
    hw_disable_irq(PIT_IRQ_NR);
    set_irq_handler(PIT_IRQ_NR, timer_irq_handler, 0);
}

static hw_timer_t pit_timer = {
#ifdef CONFIG_PC_PIT_CLOCK
    .name = "i8253p timer",
#else
    .name = "i8253 timer",
#endif
    .flags = 0,
    .freq_khz = PIT_KHZ,
    .init_hw_timer = init_pit_timer,
    .set_hw_timer = set_pit_timer,
    .get_max_interval = get_pit_timer_max_interval,
    .get_min_interval = get_pit_timer_min_interval,
    .set_timer_handler = set_pit_timer_handler,
    .shutdown_hw_timer = pit_timer_shutdown,
};

hw_timer_t *get_sys_hw_timer(void) {
    return &pit_timer;
}

#ifdef CONFIG_PC_PIT_CLOCK

static hw_clock_t pit_clock;

static prtos_s32_t init_pit_clock(void) {
    pit_clock_data.ticks = 0;
    pit_clock.flags |= HWCLOCK_ENABLED;
    return 1;
}

static hw_time_t read_pit_clock(void) {
    hw_time_t current_time, t;
    prtos_u32_t cnt;
    out_byte_port(PIT_LATCH_CNT0, PIT_MODE);
    cnt = in_byte_port(PIT_CH0);
    cnt |= in_byte_port(PIT_CH0) << 8;
    ASSERT(cnt <= PIT_PERIOD);
    t = pit_clock_data.ticks;
    current_time = PIT_PERIOD - cnt;
    return current_time + t * PIT_PERIOD;
}

static prtos_time_t read_pit_clockUsec(void) {
    return hwtime_to_duration(read_pit_clock(), PIT_HZ);
}

static hw_clock_t pit_clock = {
    .name = "PIT clock",
    .flags = 0,
    .freq_khz = PIT_KHZ,
    .init_clock = init_pit_clock,
    .get_time_usec = read_pit_clockUsec,
    .shutdown_clock = 0,
};

hw_clock_t *sys_hw_clock = &pit_clock;

#endif

#endif
