/*
 * FILE: timer.c
 *
 * RISC-V 64-bit timer driver (uses SBI for timer access)
 *
 * www.prtos.org
 */

#include <kthread.h>
#include <ktimer.h>
#include <stdc.h>
#include <processor.h>

/* SBI set_timer (legacy extension) */
static void sbi_set_timer(prtos_u64_t stime_value) {
    register prtos_u64_t a0 __asm__("a0") = stime_value;
    register prtos_u64_t a7 __asm__("a7") = 0;  /* SBI_SET_TIMER */
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static inline prtos_u64_t read_time(void) {
    prtos_u64_t val;
    __asm__ __volatile__("rdtime %0" : "=r"(val));
    return val;
}

/* Timer frequency: 10 MHz on QEMU virt */
#define TIMER_FREQ 10000000ULL
#define TICK_INTERVAL (TIMER_FREQ / 100)  /* 10ms ticks */

static prtos_time_t riscv_get_time_usec(void) {
    prtos_u64_t t = read_time();
    return (prtos_time_t)(t / 10);  /* 10MHz: 1 tick = 100ns = 0.1us */
}

static void riscv_set_hw_timer(prtos_time_t delta) {
    prtos_u64_t ticks = delta * 10;  /* usec to timer ticks (10MHz) */
    if (ticks < 100) ticks = 100;
    sbi_set_timer(read_time() + ticks);
    /* Enable timer interrupt */
    __asm__ __volatile__("csrs sie, %0" : : "r"(1UL << 5));
}

static prtos_time_t riscv_get_max_interval(void) {
    return 1000000LL;  /* 1 second in usec */
}

static prtos_time_t riscv_get_min_interval(void) {
    return 10;  /* 10us minimum */
}

static prtos_s32_t riscv_init_hw_timer(void) {
    /* Don't schedule initial timer tick - let the scheduler handle it */
    /* Enable supervisor timer interrupt (bit 5) and software interrupt (bit 1 for IPI) */
    __asm__ __volatile__("csrs sie, %0" : : "r"((1UL << 5) | (1UL << 1)));
    return 0;
}

/* Timer handler registered by the kernel timer subsystem */
timer_handler_t riscv_timer_handler;

static timer_handler_t riscv_set_timer_handler(timer_handler_t handler) {
    timer_handler_t old = riscv_timer_handler;
    riscv_timer_handler = handler;
    return old;
}

static hw_timer_t riscv_timer = {
    .name = "riscv-timer",
    .flags = HWTIMER_ENABLED | HWTIMER_PER_CPU,
    .freq_khz = 10000,  /* 10 MHz */
    .init_hw_timer = riscv_init_hw_timer,
    .set_hw_timer = riscv_set_hw_timer,
    .get_max_interval = riscv_get_max_interval,
    .get_min_interval = riscv_get_min_interval,
    .set_timer_handler = riscv_set_timer_handler,
};

hw_timer_t *get_sys_hw_timer(void) {
    return &riscv_timer;
}

static prtos_s32_t riscv_init_clock(void) {
    return 0;
}

static hw_clock_t riscv_clock = {
    .name = "riscv-clock",
    .flags = HWCLOCK_ENABLED,
    .freq_khz = 10000,
    .init_clock = riscv_init_clock,
    .get_time_usec = riscv_get_time_usec,
};

hw_clock_t *sys_hw_clock = &riscv_clock;

/* init_hpet - compatibility name used by common setup code */
void init_hpet(void) {
}

void init_riscv_timer(void) {
    /* Timer arming is handled by the scheduler via set_hw_timer.
     * Just enable the supervisor timer interrupt bit in sie. */
    __asm__ __volatile__("csrs sie, %0" : : "r"(1UL << 5));
}

/* cpu_khz defined in common setup.c */
