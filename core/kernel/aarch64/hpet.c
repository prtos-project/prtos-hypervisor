/*
 * FILE: hpet.c
 *
 * High Precision Event Timer
 *
 * www.prtos.org
 */
#ifdef CONFIG_HPET

#include <assert.h>
#include <boot.h>
#include <kdevice.h>
#include <ktimer.h>
#include <smp.h>
#include <stdc.h>
#include <processor.h>
#include <virtmm.h>
#include <vmmap.h>
#include <arch/io.h>

static prtos_u64_t prtos_boot_count;


#ifdef CONFIG_HPET_TIMER

#endif /*CONFIG_HPET_TIMER*/

#ifdef CONFIG_HPET_CLOCK

static hw_clock_t armv8_clock;

extern prtos_u64_t prtos_get_current_circle();
extern prtos_s64_t prtos_get_s_time(prtos_u64_t prtos_boot_count);
static prtos_s32_t init_armv8_clock(void) {
    armv8_clock.flags |= HWCLOCK_ENABLED;
    prtos_boot_count = prtos_get_current_circle();
    return 1;
}

static prtos_time_t read_armv8_clock_usec(void) {
    return prtos_get_s_time(prtos_boot_count) / 1000; // Assuming prtos_get_s_time() returns nanoseconds
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

#endif /*CONFIG_armv8_clock*/

unsigned int get_frequency_khz_prtos(void);
__VBOOT void init_hpet(void) {

     armv8_clock.freq_khz = get_frequency_khz_prtos();
}

#endif /*CONFIG_HPET*/
