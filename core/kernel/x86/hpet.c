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

#define FEMTOSECS_PER_SEC 1000000000000000ULL

#define HPET_IRQ_NR 0
#define HPET_HZ 14318180UL
#define HPET_KHZ 14318UL

#define HPET_PHYS_ADDR 0xfed00000

#define HPET_GCID (0x000)
#define HPET_GCID_VID 0xffff0000
#define HPET_VENDOR_ID(ID) ((HPET_GCID_VID & (ID)) >> 16)
#define HPET_GCID_LRC 0x00008000
#define HPET_GCID_CS 0x00002000
#define HPET_GCID_NT 0x00001f00
#define HPET_GCID_RID 0x000000ff

#define HPET_CTP (0x004)

#define HPET_GCFG (0x010)
#define HPET_GCFG_LRE 0x00000002
#define HPET_GCFG_EN 0x00000001

#define HPET_GIS (0x020)
#define HPET_GIS_T2 0x00000004
#define HPET_GIS_T1 0x00000002
#define HPET_GIS_T0 0x00000001

#define HPET_MCV (0x0f0)
#define HPET_MCV_L (0x0f0)
#define HPET_MCV_H (0x0f4)

#define HPET_T0CC (0x100)
#define HPET_T1CC (0x120)
#define HPET_T2CC (0x140)
#define HPET_TnCC_IT 0x00000002
#define HPET_TnCC_IE 0x00000004
#define HPET_TnCC_TYP 0x00000008
#define HPET_TnCC_PIC 0x00000010
#define HPET_TnCC_TS 0x00000020
#define HPET_TnCC_TVS 0x00000040
#define HPET_TnCC_T32M 0x00000100
#define HPET_TnCC_IR(N) (((N)&0x1f) << 9)
#define HPET_TnCC_IMASK (0x1f << 9)

#define HPET_T0ROUTE (0x104)
#define HPET_T1ROUTE (0x124)
#define HPET_T2ROUTE (0x144)

#define HPET_T0CV (0x108)
#define HPET_T0CV_L (0x108)
#define HPET_T0CV_H (0x10C)

#define HPET_T1CV (0x128)
#define HPET_T1CV_L (0x128)
#define HPET_T1CV_H (0x12C)

#define HPET_T2CV (0x148)
#define HPET_T2CV_L (0x148)
#define HPET_T2CV_H (0x14C)

static prtos_address_t hpet_virt_addr;
static prtos_u64_t hpet_freq_hz;

RESERVE_PHYSPAGES(HPET_PHYS_ADDR, 1);

static inline prtos_u32_t hpet_read_reg32(prtos_u32_t reg) {
    prtos_u32_t ret;
    __asm__ __volatile__("movl %1, %0\n\t" : "=r"(ret) : "m"(*(volatile prtos_u32_t *)(hpet_virt_addr + reg)) : "memory");
    return ret;
}

static inline void hpet_write_reg32(prtos_u32_t reg, prtos_u32_t val) {
    __asm__ __volatile__("movl %0, %1\n\t" : : "r"(val), "m"(*(volatile prtos_u32_t *)(hpet_virt_addr + reg)) : "memory");
}

static inline prtos_u64_t hpet_read_reg64(prtos_u32_t reg) {
    prtos_u64_t ret;
    ret = *((volatile prtos_u64_t *)(hpet_virt_addr + reg));
    return ret;
}

#ifdef CONFIG_HPET_TIMER

RESERVE_HWIRQ(HPET_IRQ_NR);

static hw_timer_t hpet_timer;
static timer_handler_t hpet_handler;

static void timer_irq_handler(cpu_ctxt_t *ctxt, void *irqData) {
    if (hpet_handler) (*hpet_handler)();
    hw_enable_irq(HPET_IRQ_NR);
}

static prtos_s32_t init_hpet_timer(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_u32_t cfg;

    info->cpu.global_irq_mask &= ~(1 << HPET_IRQ_NR);
    set_irq_handler(HPET_IRQ_NR, timer_irq_handler, 0);

    cfg = HPET_TnCC_IR(HPET_IRQ_NR) | HPET_TnCC_IE | HPET_TnCC_T32M;
    hpet_write_reg32(HPET_T0CC, cfg);
    if ((hpet_read_reg32(HPET_T0CC) & HPET_TnCC_IMASK) != HPET_TnCC_IR(HPET_IRQ_NR)) {
        PWARN("HPET interrupt not routed\n");
        return -1;
    }

    hpet_timer.freq_khz |= hpet_freq_hz / 1000;
    hpet_timer.flags |= HWTIMER_ENABLED;
    hw_enable_irq(HPET_IRQ_NR);

    return 1;
}

static void set_hpet_timer(prtos_time_t interval) {
    prtos_u32_t hpetCounter = (interval * HPET_HZ) / USECS_PER_SEC;
    prtos_u32_t cnt;

    // Overflow is not a problem. The counter will wrap and generate an interrupt
    // in the same interval
    cnt = hpet_read_reg32(HPET_MCV);
    cnt += hpetCounter;
    hpet_write_reg32(HPET_T0CV, cnt);
}

static prtos_time_t get_hpet_timer_max_interval(void) {
    return (0xFFFFFFFFULL * USECS_PER_SEC) / HPET_HZ;
}

static prtos_time_t get_hpet_timer_min_interval(void) {
    return 2;  // 2 us shot
}

static timer_handler_t set_hpet_timer_handler(timer_handler_t timer_handler) {
    timer_handler_t OldHpetUserHandler = hpet_handler;
    hpet_handler = timer_handler;
    return OldHpetUserHandler;
}

static void hpet_timer_shutdown(void) {
    prtos_u32_t cfg;
    hpet_timer.flags &= ~HWTIMER_ENABLED;
    hw_disable_irq(HPET_IRQ_NR);
    set_irq_handler(HPET_IRQ_NR, timer_irq_handler, 0);

    cfg = hpet_read_reg32(HPET_GCFG);
    cfg &= ~HPET_GCFG_EN;
    hpet_write_reg32(HPET_GCFG, cfg);
}

static hw_timer_t hpet_timer = {
    .name = "HPET timer",
    .flags = 0,
    .freq_khz = 0,
    .init_hw_timer = init_hpet_timer,
    .set_hw_timer = set_hpet_timer,
    .get_max_interval = get_hpet_timer_max_interval,
    .get_min_interval = get_hpet_timer_min_interval,
    .set_timer_handler = set_hpet_timer_handler,
    .shutdown_hw_timer = hpet_timer_shutdown,
};

hw_timer_t *get_sys_hw_timer(void) {
    return &hpet_timer;
}

#endif /*CONFIG_HPET_TIMER*/

#ifdef CONFIG_HPET_CLOCK

static hw_clock_t hpet_clock;

static prtos_s32_t init_hpet_clock(void) {
    hpet_clock.flags |= HWCLOCK_ENABLED;
    if (!(hpet_read_reg32(HPET_GCID) & HPET_GCID_CS)) {
        PWARN("HPET Counter size 32-bits\n");
        return -1;
    }
    return 1;
}

static prtos_time_t read_hpet_clock_usec(void) {
    hw_time_t hpetTime = hpet_read_reg64(HPET_MCV);
    return hwtime_to_duration(hpetTime, hpet_freq_hz);
}

static hw_clock_t hpet_clock = {
    .name = "HPET clock",
    .flags = 0,
    .freq_khz = 0,
    .init_clock = init_hpet_clock,
    .get_time_usec = read_hpet_clock_usec,
    .shutdown_clock = 0,
};

hw_clock_t *sys_hw_clock = &hpet_clock;

#endif /*CONFIG_HPET_CLOCK*/

__VBOOT void init_hpet(void) {
    prtos_u32_t cfg;

    hpet_virt_addr = vmm_alloc(1);
    vm_map_page(HPET_PHYS_ADDR, hpet_virt_addr, _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_GLOBAL | _PG_ARCH_PCD);

    /*if ((HPET_GCID_VID & hpet_read_reg32(HPET_GCID)) != HPET_VENDOR_ID(CONFIG_HPET_VENDOR_ID)) {
        PWARN("Bad HPET Vendor ID: %x\n", (HPET_GCID_VID & hpet_read_reg32(HPET_GCID)));
    }*/
    cfg = HPET_VENDOR_ID(hpet_read_reg32(HPET_GCID));
    eprintf("HPET Vendor ID %x\n", cfg);

    // Enable timer
    cfg = hpet_read_reg32(HPET_GCFG);
    cfg |= HPET_GCFG_EN | HPET_GCFG_LRE;
    hpet_write_reg32(HPET_GCFG, cfg);

    hpet_freq_hz = hpet_read_reg32(HPET_CTP);
    hpet_freq_hz = FEMTOSECS_PER_SEC / hpet_freq_hz;
    hpet_clock.freq_khz = hpet_freq_hz / 1000;
}

#endif /*CONFIG_HPET*/
