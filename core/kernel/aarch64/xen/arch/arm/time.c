#include <xen/xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/time.c
 *
 * Time and timer support, using the ARM Generic Timer interfaces
 *
 * Tim Deegan <tim@xen.org>
 * Copyright (c) 2011 Citrix Systems.
 */

#include <xen/console.h>
#include <xen/device_tree.h>
#include <xen/init.h>
#include <xen/irq.h>
#include <xen/lib.h>
#include <xen/mm.h>
#include <xen/softirq.h>
#include <xen/sched.h>
#include <xen/time.h>
#include <xen/delay.h>
#include <xen/sched.h>
#include <xen/event.h>
#include <xen/acpi.h>
#include <xen/cpu.h>
#include <xen/notifier.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/vgic.h>
#include <asm/cpufeature.h>
#include <asm/platform.h>

uint64_t __read_mostly boot_count;

/* For fine-grained timekeeping, we use the ARM "Generic Timer", a
 * register-mapped time source in the SoC. */
unsigned long __read_mostly xen_cpu_khz;  /* CPU clock frequency in kHz. */

uint32_t __read_mostly timer_dt_clock_frequency;

static unsigned int timer_irq[MAX_TIMER_PPI];

unsigned int timer_get_irq(enum timer_ppi ppi)
{
    ASSERT(ppi >= TIMER_PHYS_SECURE_PPI && ppi < MAX_TIMER_PPI);

    return timer_irq[ppi];
}

/*static inline*/ s_time_t ticks_to_ns(uint64_t ticks)
{
    return muldiv64(ticks, SECONDS(1), 1000 * xen_cpu_khz);
}

/*static inline*/ uint64_t ns_to_ticks(s_time_t ns)
{
    return muldiv64(ns, 1000 * xen_cpu_khz, SECONDS(1));
}

static __initdata struct dt_device_node *timer;

#ifdef CONFIG_ACPI
#else
static void __init preinit_acpi_xen_time(void) { }
#endif

static void __init validate_timer_frequency(void)
{
    /*
     * ARM ARM does not impose any strict limit on the range of allowable
     * system counter frequencies. However, we operate under the assumption
     * that xen_cpu_khz must not be 0.
     */
    if ( !xen_cpu_khz )
        panic("Timer frequency is less than 1 KHz\n");
}

/* Set up the timer on the boot CPU (early init function) */
static void __init preinit_dt_xen_time(void)
{
    static const struct dt_device_match timer_ids[] __initconst =
    {
        DT_MATCH_TIMER,
        { /* sentinel */ },
    };
    int res;
    u32 rate;

    timer = dt_find_matching_node(NULL, timer_ids);
    if ( !timer )
        panic("Unable to find a compatible timer in the device tree\n");

    dt_device_set_used_by(timer, DOMID_XEN);

    res = dt_property_read_u32(timer, "clock-frequency", &rate);
    if ( res )
    {
        xen_cpu_khz = rate / 1000;
        validate_timer_frequency();
        timer_dt_clock_frequency = rate;
    }
}
extern int prtos_kernel_run;
void __init preinit_xen_time(void)
{
    int res;
    if (!prtos_kernel_run) {
        /* Initialize all the generic timers presented in GTDT */
        if (acpi_disabled)
            preinit_dt_xen_time();
        else
            preinit_acpi_xen_time();
    }
    if ( !xen_cpu_khz )
    {
        xen_cpu_khz = (READ_SYSREG(CNTFRQ_EL0) & CNTFRQ_MASK) / 1000;
        validate_timer_frequency();
    }

    res = platform_init_time();
    if ( res )
        panic("Timer: Cannot initialize platform timer\n");

    boot_count = get_cycles();
}

void init_global_clock_for_prtos(void) {
    /* Initialize the global clock for PRTOS */
    xen_cpu_khz = (READ_SYSREG(CNTFRQ_EL0) & CNTFRQ_MASK) / 1000;
    validate_timer_frequency();
    printk("Global clock initialized: %lu KHz\n", xen_cpu_khz);
    boot_count = get_cycles();
}

unsigned int get_frequency_khz_prtos(void) {
    xen_cpu_khz = (READ_SYSREG(CNTFRQ_EL0) & CNTFRQ_MASK) / 1000;
    if (!xen_cpu_khz) panic("Timer frequency is not initialized\n");

    return xen_cpu_khz;
}

static void __init init_dt_xen_time(void)
{
    int res;
    unsigned int i;
    bool has_names;
    static const char * const timer_irq_names[MAX_TIMER_PPI] __initconst = {
        [TIMER_PHYS_SECURE_PPI] = "sec-phys",
        [TIMER_PHYS_NONSECURE_PPI] = "phys",
        [TIMER_VIRT_PPI] = "virt",
        [TIMER_HYP_PPI] = "hyp-phys",
        [TIMER_HYP_VIRT_PPI] = "hyp-virt",
    };

    has_names = dt_property_read_bool(timer, "interrupt-names");

    /* Retrieve all IRQs for the timer */
    for ( i = TIMER_PHYS_SECURE_PPI; i < MAX_TIMER_PPI; i++ )
    {
        if ( has_names )
            res = platform_get_irq_byname(timer, timer_irq_names[i]);
        else
            res = platform_get_irq(timer, i);

        if ( res > 0 )
            timer_irq[i] = res;
        /*
         * Do not panic if "hyp-virt" PPI is not found, since it's not
         * currently used.
         */
        else if ( i != TIMER_HYP_VIRT_PPI )
            panic("Timer: Unable to retrieve IRQ %u from the device tree\n", i);
    }
}

/* Set up the timer on the boot CPU (late init function) */
int __init init_xen_time(void)
{
    if ( acpi_disabled )
        init_dt_xen_time();

    /* Check that this CPU supports the Generic Timer interface */
    if ( !cpu_has_gentimer )
        panic("CPU does not support the Generic Timer v1 interface\n");

    printk("Generic Timer IRQ: phys=%u hyp=%u virt=%u Freq: %lu KHz\n",
           timer_irq[TIMER_PHYS_NONSECURE_PPI],
           timer_irq[TIMER_HYP_PPI],
           timer_irq[TIMER_VIRT_PPI],
           xen_cpu_khz);

    return 0;
}

int __init init_xen_time_prtos(void)
{
    // if ( acpi_disabled )
    //     init_dt_xen_time();
    timer_irq[TIMER_PHYS_SECURE_PPI] = 0x1d;
    timer_irq[TIMER_PHYS_NONSECURE_PPI] = 0x1e; // PRTOS: Use a fixed IRQ for the physical timer
    timer_irq[TIMER_VIRT_PPI] = 0x1b; // PRTOS: Use a fixed IRQ for the hypervisor timer
    timer_irq[TIMER_HYP_PPI] = 0x1a; // PRTOS: Use a fixed IRQ for the virtual timer   

    /* Check that this CPU supports the Generic Timer interface */
    if ( !cpu_has_gentimer )
        panic("CPU does not support the Generic Timer v1 interface\n");

    printk("Generic Timer IRQ: phys=%u hyp=%u virt=%u Freq: %lu KHz\n",
           timer_irq[TIMER_PHYS_NONSECURE_PPI],
           timer_irq[TIMER_HYP_PPI],
           timer_irq[TIMER_VIRT_PPI],
           xen_cpu_khz);

    return 0;
}



/* Return number of nanoseconds since boot */
s_time_t get_s_time(void)
{
    uint64_t ticks = get_cycles() - boot_count;
    return ticks_to_ns(ticks);
}

/* Set the timer to wake us up at a particular time.
 * Timeout is a Xen system time (nanoseconds since boot); 0 disables the timer.
 * Returns 1 on success; 0 if the timeout is too soon or is in the past. */
int reprogram_timer(s_time_t timeout)
{
    uint64_t deadline;

    if ( timeout == 0 )
    {
        WRITE_SYSREG(0, CNTHP_CTL_EL2);
        return 1;
    }

    deadline = ns_to_ticks(timeout) + boot_count;
    WRITE_SYSREG64(deadline, CNTHP_CVAL_EL2);
    WRITE_SYSREG(CNTx_CTL_ENABLE, CNTHP_CTL_EL2);
    isb();

    /* No need to check for timers in the past; the Generic Timer fires
     * on a signed 63-bit comparison. */
    return 1;
}

/* Handle the firing timer */
static void htimer_interrupt(int irq, void *dev_id)
{
    printk("htimer_interrupt: Hypervisor timer interrupt on CPU %u %d\n", smp_processor_id(), irq);
    if ( unlikely(!(READ_SYSREG(CNTHP_CTL_EL2) & CNTx_CTL_PENDING)) )
        return;

    perfc_incr(hyp_timer_irqs);

    /* Signal the generic timer code to do its work */
    raise_softirq(TIMER_SOFTIRQ);

    /* Disable the timer to avoid more interrupts */
    WRITE_SYSREG(0, CNTHP_CTL_EL2);
}

// #if CONFIG_STATIC_IRQ_ROUTING

extern void enable_timer_prtos(void);
void prtos_gicv3_host_irq_end(int irq);
void static_htimer_isr(int irq) {
    prtos_gicv3_host_irq_end(irq);
    enable_timer_prtos();
}

int prtos_get_gpu_khz(void) {
    int prtos_cpu_khz;
    prtos_cpu_khz = (READ_SYSREG(CNTFRQ_EL0) & CNTFRQ_MASK) / 1000;
    if (!prtos_cpu_khz) printk("PRTOS Timer frequency is not initialized\n");
    return prtos_cpu_khz;
}

uint64_t prtos_get_current_circle() {
         return get_cycles();
    }

int64_t prtos_get_s_time(uint64_t prtos_boot_count)
{
    uint64_t ticks = get_cycles() - prtos_boot_count;
    return ticks_to_ns(ticks);
}

int __arch_get_local_id(void) {
    return smp_processor_id();  
}

// #endif // CONFIG_STATIC_IRQ_ROUTING

static void vtimer_interrupt(int irq, void *dev_id)
{
    /*
     * Edge-triggered interrupts can be used for the virtual timer. Even
     * if the timer output signal is masked in the context switch, the
     * GIC will keep track that of any interrupts raised while IRQS are
     * disabled. As soon as IRQs are re-enabled, the virtual interrupt
     * will be injected to Xen.
     *
     * If an IDLE vCPU was scheduled next then we should ignore the
     * interrupt.
     */
    if ( unlikely(is_idle_vcpu(current)) )
        return;

    perfc_incr(virt_timer_irqs);

    current->arch.virt_timer.ctl = READ_SYSREG(CNTV_CTL_EL0);
    WRITE_SYSREG(current->arch.virt_timer.ctl | CNTx_CTL_MASK, CNTV_CTL_EL0);
    vgic_inject_irq(current->domain, current, current->arch.virt_timer.irq, true);
}

/*
 * Arch timer interrupt really ought to be level triggered, since the
 * design of the timer/comparator mechanism is based around that
 * concept.
 *
 * However some firmware (incorrectly) describes the interrupts as
 * edge triggered and, worse, some hardware allows us to program the
 * interrupt controller as edge triggered.
 *
 * Check each interrupt and warn if we find ourselves in this situation.
 */
static void check_timer_irq_cfg(unsigned int irq, const char *which)
{
    struct irq_desc *desc = irq_to_desc(irq);

    /*
     * The interrupt controller driver will update desc->arch.type with
     * the actual type which ended up configured in the hardware.
     */
    if ( desc->arch.type & IRQ_TYPE_LEVEL_MASK )
        return;

    printk(XENLOG_WARNING
           "WARNING: %s-timer IRQ%u is not level triggered.\n", which, irq);
}

/* Set up the timer interrupt on this CPU */
void init_timer_interrupt(void)
{
    /* Sensible defaults */
    WRITE_SYSREG64(0, CNTVOFF_EL2);     /* No VM-specific offset */
    /* Do not let the VMs program the physical timer, only read the physical counter */
    WRITE_SYSREG(CNTHCTL_EL2_EL1PCTEN, CNTHCTL_EL2);
    WRITE_SYSREG(0, CNTP_CTL_EL0);    /* Physical timer disabled */
    WRITE_SYSREG(0, CNTHP_CTL_EL2);   /* Hypervisor's timer disabled */
    isb();

    request_irq(timer_irq[TIMER_HYP_PPI], 0, htimer_interrupt,
                "hyptimer", NULL);
    request_irq(timer_irq[TIMER_VIRT_PPI], 0, vtimer_interrupt,
                   "virtimer", NULL);

    check_timer_irq_cfg(timer_irq[TIMER_HYP_PPI], "hypervisor");
    check_timer_irq_cfg(timer_irq[TIMER_VIRT_PPI], "virtual");
    check_timer_irq_cfg(timer_irq[TIMER_PHYS_NONSECURE_PPI], "NS-physical");
}

void init_timer_interrupt_prtos(void) {
    /* Sensible defaults */
    WRITE_SYSREG64(0, CNTVOFF_EL2); /* No VM-specific offset */
    /* Do not let the VMs program the physical timer, only read the physical counter */
    WRITE_SYSREG(CNTHCTL_EL2_EL1PCTEN, CNTHCTL_EL2);
    WRITE_SYSREG(0, CNTP_CTL_EL0);  /* Physical timer disabled */
    WRITE_SYSREG(0, CNTHP_CTL_EL2); /* Hypervisor's timer disabled */
    isb();

    request_irq_prtos(26, 0, htimer_interrupt, "hyptimer", NULL);
    request_irq_prtos(27, 0, vtimer_interrupt, "virtimer", NULL);

    check_timer_irq_cfg(timer_irq[TIMER_HYP_PPI], "hypervisor");
    check_timer_irq_cfg(timer_irq[TIMER_VIRT_PPI], "virtual");
    // check_timer_irq_cfg(timer_irq[TIMER_PHYS_NONSECURE_PPI], "NS-physical");
}

/*
 * Revert actions done in init_timer_interrupt that are required to properly
 * disable this CPU.
 */
static void deinit_timer_interrupt(void)
{
    WRITE_SYSREG(0, CNTP_CTL_EL0);    /* Disable physical timer */
    WRITE_SYSREG(0, CNTHP_CTL_EL2);   /* Disable hypervisor's timer */
    isb();

    release_irq(timer_irq[TIMER_HYP_PPI], NULL);
    release_irq(timer_irq[TIMER_VIRT_PPI], NULL);
}

/* Wait a set number of microseconds */
void udelay(unsigned long usecs)
{
    s_time_t deadline = get_s_time() + 1000 * (s_time_t) usecs;
    while ( get_s_time() - deadline < 0 )
        ;
    dsb(sy);
    isb();
}

/* VCPU PV timers. */
void send_timer_event(struct vcpu *v)
{
    send_guest_vcpu_virq(v, VIRQ_TIMER);
}

/* VCPU PV clock. */
void update_vcpu_system_time(struct vcpu *v)
{
    /* XXX update shared_info->wc_* */
}

void force_update_vcpu_system_time(struct vcpu *v)
{
    update_vcpu_system_time(v);
}

void domain_set_time_offset(struct domain *d, int64_t time_offset_seconds)
{
    d->time_offset.seconds = time_offset_seconds;
    d->time_offset.set = true;
    /* XXX update guest visible wallclock time */
}

static int cpu_time_callback(struct notifier_block *nfb,
                             unsigned long action,
                             void *hcpu)
{
    switch ( action )
    {
    case CPU_DYING:
        deinit_timer_interrupt();
        break;
    default:
        break;
    }

    return NOTIFY_DONE;
}

static struct notifier_block cpu_time_nfb = {
    .notifier_call = cpu_time_callback,
};

static int __init cpu_time_notifier_init(void)
{
    register_cpu_notifier(&cpu_time_nfb);

    return 0;
}
__initcall(cpu_time_notifier_init);

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
