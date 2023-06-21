/*
 * FILE: tsc.c
 *
 * TSC driver
 *
 * www.prtos.org
 *
 */

#ifdef CONFIG_TSC_CLOCK
#include <ktimer.h>
#include <processor.h>
#include <stdc.h>

static hw_clock_t tsc_clock;

static prtos_s32_t init_tsc_clock(void) {
    tsc_clock.freq_khz = cpu_khz;
    tsc_clock.flags |= HWCLOCK_ENABLED;
    return 1;
}

static prtos_time_t read_tsc_clock_usec(void) {
    hw_time_t ts_current_time = read_tsc_load_low();
    return hwtime_to_duration(ts_current_time, tsc_clock.freq_khz * 1000);
}

static hw_clock_t tsc_clock = {
    .name = "TSC clock",
    .flags = 0,
    .freq_khz = 0,
    .init_clock = init_tsc_clock,
    .get_time_usec = read_tsc_clock_usec,
    .shutdown_clock = 0,
};

hw_clock_t *sys_hw_clock = &tsc_clock;

#endif
