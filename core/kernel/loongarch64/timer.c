/*
 * FILE: timer.c
 *
 * LoongArch 64-bit timer driver
 * Uses the LoongArch stable counter and TCFG/TVAL CSRs.
 *
 * http://www.prtos.org/
 */

#include <kthread.h>
#include <ktimer.h>
#include <stdc.h>
#include <processor.h>

/* CSR numbers for timer */
#define CSR_TCFG  0x41
#define CSR_TVAL  0x42
#define CSR_TICLR 0x44

/* TCFG bits */
#define TCFG_EN     (1UL << 0)
#define TCFG_PERIOD (1UL << 1)

static inline prtos_u64_t read_stable_counter(void) {
    prtos_u64_t val;
    __asm__ __volatile__("rdtime.d %0, $zero" : "=r"(val));
    return val;
}

/* Timer frequency: 100 MHz on QEMU virt */
#define TIMER_FREQ 100000000ULL

static prtos_time_t loongarch_get_time_usec(void) {
    prtos_u64_t t = read_stable_counter();
    return (prtos_time_t)(t / 100);  /* 100MHz: 1 tick = 10ns = 0.01us */
}

static void loongarch_set_hw_timer(prtos_time_t delta) {
    prtos_u64_t ticks = delta * 100;  /* usec to timer ticks (100MHz) */
    if (ticks < 1000) ticks = 1000;
    /* LoongArch TCFG: TVAL is reloaded from InitVal only when En transitions
     * from 0 to 1.  If the timer is already running (En=1), we must first
     * disable it so the next write triggers a proper 0→1 transition. */
    prtos_u64_t zero = 0;
    __asm__ __volatile__("csrwr %0, 0x41" : "+r"(zero));  /* disable timer */
    prtos_u64_t tcfg = TCFG_EN | (ticks << 2);  /* EN=1, PERIOD=0, InitVal=ticks */
    __asm__ __volatile__("csrwr %0, 0x41" : "+r"(tcfg));  /* re-enable */
}

static prtos_time_t loongarch_get_max_interval(void) {
    return 1000000LL;  /* 1 second in usec */
}

static prtos_time_t loongarch_get_min_interval(void) {
    return 10;  /* 10us minimum */
}

static prtos_s32_t loongarch_init_hw_timer(void) {
    /* Clear any pending timer interrupt */
    prtos_u64_t val = 1;
    __asm__ __volatile__("csrwr %0, 0x44" : "+r"(val));  /* TICLR: write 1 to clear */
    return 0;
}

/* Timer handler registered by the kernel timer subsystem */
timer_handler_t loongarch_timer_handler;

static timer_handler_t loongarch_set_timer_handler(timer_handler_t handler) {
    timer_handler_t old = loongarch_timer_handler;
    loongarch_timer_handler = handler;
    return old;
}

static hw_timer_t loongarch_timer = {
    .name = "loongarch-timer",
    .flags = HWTIMER_ENABLED | HWTIMER_PER_CPU,
    .freq_khz = 100000,  /* 100 MHz */
    .init_hw_timer = loongarch_init_hw_timer,
    .set_hw_timer = loongarch_set_hw_timer,
    .get_max_interval = loongarch_get_max_interval,
    .get_min_interval = loongarch_get_min_interval,
    .set_timer_handler = loongarch_set_timer_handler,
};

hw_timer_t *get_sys_hw_timer(void) {
    return &loongarch_timer;
}

static prtos_s32_t loongarch_init_clock(void) {
    return 0;
}

static hw_clock_t loongarch_clock = {
    .name = "loongarch-clock",
    .flags = HWCLOCK_ENABLED,
    .freq_khz = 100000,
    .init_clock = loongarch_init_clock,
    .get_time_usec = loongarch_get_time_usec,
};

hw_clock_t *sys_hw_clock = &loongarch_clock;

/* init_hpet - compatibility name used by common setup code */
void init_hpet(void) {
}

void init_loongarch_timer(void) {
    /* Clear pending timer interrupt */
    prtos_u64_t val = 1;
    __asm__ __volatile__("csrwr %0, 0x44" : "+r"(val));
}
