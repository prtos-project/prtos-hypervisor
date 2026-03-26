/* PRTOS time & timer - consolidated */
/* === BEGIN INLINED: time.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/time.c
 *
 * Time and timer support, using the ARM Generic Timer interfaces
 *
 * Tim Deegan <tim@xen.org>
 * Copyright (c) 2011 Citrix Systems.
 */

#include <prtos_console.h>
#include <prtos_device_tree.h>
#include <prtos_init.h>
#include <prtos_irq.h>
#include <prtos_lib.h>
#include <prtos_mm.h>
#include <prtos_softirq.h>
#include <prtos_sched.h>
#include <prtos_time.h>
#include <prtos_delay.h>
#include <prtos_sched.h>
#include <prtos_event.h>
#include <prtos_acpi.h>
#include <prtos_cpu.h>
#include <prtos_notifier.h>
#include <asm_system.h>
#include <asm_time.h>
#include <asm_vgic.h>
#include <asm_cpufeature.h>
#include <asm_platform.h>

uint64_t __read_mostly boot_count;

/* For fine-grained timekeeping, we use the ARM "Generic Timer", a
 * register-mapped time source in the SoC. */
unsigned long __read_mostly prtos_cpu_khz; /* CPU clock frequency in kHz. */

uint32_t __read_mostly timer_dt_clock_frequency;

static unsigned int timer_irq[MAX_TIMER_PPI];

unsigned int timer_get_irq(enum timer_ppi ppi) {
    ASSERT(ppi >= TIMER_PHYS_SECURE_PPI && ppi < MAX_TIMER_PPI);

    return timer_irq[ppi];
}

/*static inline*/ s_time_t ticks_to_ns(uint64_t ticks) {
    return muldiv64(ticks, SECONDS(1), 1000 * prtos_cpu_khz);
}

/*static inline*/ uint64_t ns_to_ticks(s_time_t ns) {
    return muldiv64(ns, 1000 * prtos_cpu_khz, SECONDS(1));
}

static __initdata struct dt_device_node *timer;

#ifdef CONFIG_ACPI
#else
#endif

static void __init validate_timer_frequency(void) {
    /*
     * ARM ARM does not impose any strict limit on the range of allowable
     * system counter frequencies. However, we operate under the assumption
     * that prtos_cpu_khz must not be 0.
     */
    if (!prtos_cpu_khz) panic("Timer frequency is less than 1 KHz\n");
}

void __init preinit_prtos_time(void) {
    int res;

    if (!prtos_cpu_khz) {
        prtos_cpu_khz = (READ_SYSREG(CNTFRQ_EL0) & CNTFRQ_MASK) / 1000;
        validate_timer_frequency();
    }

    res = platform_init_time();
    if (res) panic("Timer: Cannot initialize platform timer\n");

    boot_count = get_cycles();
}

void init_global_clock_for_prtos(void) {
    /* Initialize the global clock for PRTOS */
    prtos_cpu_khz = (READ_SYSREG(CNTFRQ_EL0) & CNTFRQ_MASK) / 1000;
    validate_timer_frequency();
    printk("Global clock initialized: %lu KHz\n", prtos_cpu_khz);
    boot_count = get_cycles();
}

unsigned int get_frequency_khz_prtos(void) {
    prtos_cpu_khz = (READ_SYSREG(CNTFRQ_EL0) & CNTFRQ_MASK) / 1000;
    if (!prtos_cpu_khz) panic("Timer frequency is not initialized\n");

    return prtos_cpu_khz;
}

static void __init init_dt_prtos_time(void) {
    int res;
    unsigned int i;
    bool has_names;
    static const char *const timer_irq_names[MAX_TIMER_PPI] __initconst = {
        [TIMER_PHYS_SECURE_PPI] = "sec-phys", [TIMER_PHYS_NONSECURE_PPI] = "phys", [TIMER_VIRT_PPI] = "virt",
        [TIMER_HYP_PPI] = "hyp-phys",         [TIMER_HYP_VIRT_PPI] = "hyp-virt",
    };

    has_names = dt_property_read_bool(timer, "interrupt-names");

    /* Retrieve all IRQs for the timer */
    for (i = TIMER_PHYS_SECURE_PPI; i < MAX_TIMER_PPI; i++) {
        if (has_names)
            res = platform_get_irq_byname(timer, timer_irq_names[i]);
        else
            res = platform_get_irq(timer, i);

        if (res > 0) timer_irq[i] = res;
        /*
         * Do not panic if "hyp-virt" PPI is not found, since it's not
         * currently used.
         */
        else if (i != TIMER_HYP_VIRT_PPI)
            panic("Timer: Unable to retrieve IRQ %u from the device tree\n", i);
    }
}

/* Set up the timer on the boot CPU (late init function) */
int __init init_prtos_time(void) {
    if (acpi_disabled) init_dt_prtos_time();

    /* Check that this CPU supports the Generic Timer interface */
    if (!cpu_has_gentimer) panic("CPU does not support the Generic Timer v1 interface\n");

    printk("Generic Timer IRQ: phys=%u hyp=%u virt=%u Freq: %lu KHz\n", timer_irq[TIMER_PHYS_NONSECURE_PPI], timer_irq[TIMER_HYP_PPI],
           timer_irq[TIMER_VIRT_PPI], prtos_cpu_khz);

    return 0;
}

int __init init_time_prtos(void) {
    // if ( acpi_disabled )
    //     init_dt_prtos_time();
    timer_irq[TIMER_PHYS_SECURE_PPI] = 0x1d;
    timer_irq[TIMER_PHYS_NONSECURE_PPI] = 0x1e;  // PRTOS: Use a fixed IRQ for the physical timer
    timer_irq[TIMER_VIRT_PPI] = 0x1b;            // PRTOS: Use a fixed IRQ for the hypervisor timer
    timer_irq[TIMER_HYP_PPI] = 0x1a;             // PRTOS: Use a fixed IRQ for the virtual timer

    /* Check that this CPU supports the Generic Timer interface */
    if (!cpu_has_gentimer) panic("CPU does not support the Generic Timer v1 interface\n");

    printk("Generic Timer IRQ: phys=%u hyp=%u virt=%u Freq: %lu KHz\n", timer_irq[TIMER_PHYS_NONSECURE_PPI], timer_irq[TIMER_HYP_PPI],
           timer_irq[TIMER_VIRT_PPI], prtos_cpu_khz);

    return 0;
}

/* Return number of nanoseconds since boot */
s_time_t get_s_time(void) {
    uint64_t ticks = get_cycles() - boot_count;
    return ticks_to_ns(ticks);
}

/* Set the timer to wake us up at a particular time.
 * Timeout is a PRTOS system time (nanoseconds since boot); 0 disables the timer.
 * Returns 1 on success; 0 if the timeout is too soon or is in the past. */
int reprogram_timer(s_time_t timeout) {
    uint64_t deadline;

    if (timeout == 0) {
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
static void htimer_interrupt(int irq, void *dev_id) {
    if (unlikely(!(READ_SYSREG(CNTHP_CTL_EL2) & CNTx_CTL_PENDING))) return;

    perfc_incr(hyp_timer_irqs);

    /* Signal the generic timer code to do its work */
    raise_softirq(TIMER_SOFTIRQ);

    /* Disable the timer to avoid more interrupts */
    WRITE_SYSREG(0, CNTHP_CTL_EL2);
}

// #if CONFIG_STATIC_IRQ_ROUTING

extern void enable_timer_prtos(void);
void prtos_gicv3_host_irq_end(int irq);
extern void prtos_timer_irq_dispatch(int irq_nr);
static DEFINE_PER_CPU(int, htimer_pending);

void static_htimer_isr(int irq) {
    /* Disable timer to prevent level-triggered re-assertion after EOI+DIR. */
    WRITE_SYSREG(0, CNTHP_CTL_EL2);
    isb();
    /* EOI the IRQ before the handler may context-switch. */
    prtos_gicv3_host_irq_end(irq);
    /* Defer the actual timer dispatch to after the GIC loop exits
     * (via static_htimer_deferred).  This prevents schedule() from
     * re-arming the timer inside the GIC loop, which would cause
     * infinite re-entry. */
    this_cpu(htimer_pending) = irq;
}

/* Called from static_gic_interrupt after the GIC loop exits. */
void static_htimer_deferred(void) {
    if (this_cpu(htimer_pending)) {
        int irq = this_cpu(htimer_pending);
        this_cpu(htimer_pending) = 0;
        prtos_timer_irq_dispatch(irq);
    }
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

int64_t prtos_get_s_time(uint64_t prtos_boot_count) {
    uint64_t ticks = get_cycles() - prtos_boot_count;
    return ticks_to_ns(ticks);
}

int __arch_get_local_id(void) {
    return smp_processor_id();
}

// #endif // CONFIG_STATIC_IRQ_ROUTING

/*
 * prtos_vtimer_inject_via_lr - Inject virtual timer IRQ via GICv3 List Register.
 *
 * For PRTOS partitions using hardware-assisted virtualization (no para-virt),
 * the virtual timer interrupt is delivered directly via the GICv3 virtual
 * CPU interface (ICH_LR) instead of PRTOS's PCT-redirect mechanism.
 *
 * The guest (e.g. FreeRTOS) handles the interrupt natively via VBAR_EL1
 * and acknowledges it using ICC_IAR1_EL1/ICC_EOIR1_EL1.
 */
static void prtos_vtimer_inject_via_lr(void) {
    uint64_t ctl = READ_SYSREG(CNTV_CTL_EL0);
    /* Mask the timer to prevent level-triggered re-assertion after EOI */
    WRITE_SYSREG(ctl | CNTx_CTL_MASK, CNTV_CTL_EL0);
    isb();

    /*
     * Write ICH_LR0_EL2 with virtual IRQ 27 (GUEST_TIMER_VIRT_PPI):
     *   [63:62] = 0b01 (State = Pending)
     *   [60]    = 1    (Group 1)
     *   [55:48] = 0    (Priority 0, highest)
     *   [31:0]  = 27   (Virtual INTID = GUEST_TIMER_VIRT_PPI)
     */
    uint64_t lr_val = (1ULL << 62) | (1ULL << 60) | 27;
    WRITE_SYSREG(lr_val, ICH_LR0_EL2);
    isb();
}

static void vtimer_interrupt(int irq, void *dev_id) {
    /*
     * PRTOS hw-virt: Always inject virtual timer via GICv3 List Register.
     * The guest handles the interrupt natively via VBAR_EL1 and ICC_*
     * system registers. We bypass PRTOS's vgic_inject_irq() entirely.
     */
    prtos_vtimer_inject_via_lr();
}

/* Static IRQ routing entry point for virtual timer PPI 27.
 * Called from static_gic_interrupt() in gic.c. */
void static_vtimer_isr(int irq) {
    prtos_vtimer_inject_via_lr();
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
static void check_timer_irq_cfg(unsigned int irq, const char *which) {
    struct irq_desc *desc = irq_to_desc(irq);

    /*
     * The interrupt controller driver will update desc->arch.type with
     * the actual type which ended up configured in the hardware.
     */
    if (desc->arch.type & IRQ_TYPE_LEVEL_MASK) return;

    printk(PRTOSLOG_WARNING "WARNING: %s-timer IRQ%u is not level triggered.\n", which, irq);
}

/* Set up the timer interrupt on this CPU */
void init_timer_interrupt(void) {
    /* Sensible defaults */
    WRITE_SYSREG64(0, CNTVOFF_EL2); /* No VM-specific offset */
    /* Do not let the VMs program the physical timer, only read the physical counter */
    WRITE_SYSREG(CNTHCTL_EL2_EL1PCTEN, CNTHCTL_EL2);
    WRITE_SYSREG(0, CNTP_CTL_EL0);  /* Physical timer disabled */
    WRITE_SYSREG(0, CNTHP_CTL_EL2); /* Hypervisor's timer disabled */
    isb();

    request_irq(timer_irq[TIMER_HYP_PPI], 0, htimer_interrupt, "hyptimer", NULL);
    request_irq(timer_irq[TIMER_VIRT_PPI], 0, vtimer_interrupt, "virtimer", NULL);

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
static void deinit_timer_interrupt(void) {
    WRITE_SYSREG(0, CNTP_CTL_EL0);  /* Disable physical timer */
    WRITE_SYSREG(0, CNTHP_CTL_EL2); /* Disable hypervisor's timer */
    isb();

    release_irq(timer_irq[TIMER_HYP_PPI], NULL);
    release_irq(timer_irq[TIMER_VIRT_PPI], NULL);
}

/* Wait a set number of microseconds */
void udelay(unsigned long usecs) {
    s_time_t deadline = get_s_time() + 1000 * (s_time_t)usecs;
    while (get_s_time() - deadline < 0);
    dsb(sy);
    isb();
}

/* VCPU PV timers. */
void send_timer_event(struct vcpu *v) {
    send_guest_vcpu_virq(v, VIRQ_TIMER);
}

/* VCPU PV clock. */
void update_vcpu_system_time(struct vcpu *v) {
    /* XXX update shared_info->wc_* */
}

void force_update_vcpu_system_time(struct vcpu *v) {
    update_vcpu_system_time(v);
}

void domain_set_time_offset(struct domain *d, int64_t time_offset_seconds) {
    d->time_offset.seconds = time_offset_seconds;
    d->time_offset.set = true;
    /* XXX update guest visible wallclock time */
}

static int cpu_time_callback(struct notifier_block *nfb, unsigned long action, void *hcpu) {
    switch (action) {
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

static int __init cpu_time_notifier_init(void) {
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

/* === END INLINED: time.c === */
/* === BEGIN INLINED: timer.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * timer.c
 * 
 * Copyright (c) 2002-2003 Rolf Neugebauer
 * Copyright (c) 2002-2005 K A Fraser
 */

#include <prtos_init.h>
#include <prtos_types.h>
#include <prtos_errno.h>
#include <prtos_sched.h>
#include <prtos_lib.h>
#include <prtos_param.h>
#include <prtos_smp.h>
#include <prtos_perfc.h>
#include <prtos_time.h>
#include <prtos_softirq.h>
#include <prtos_timer.h>
#include <prtos_keyhandler.h>
#include <prtos_percpu.h>
#include <prtos_cpu.h>
#include <prtos_rcupdate.h>
#include <prtos_symbols.h>
#include <asm_system.h>
#include <asm_atomic.h>

/* We program the time hardware this far behind the closest deadline. */
static unsigned int timer_slop __read_mostly = 50000; /* 50 us */
integer_param("timer_slop", timer_slop);

struct timers {
    spinlock_t     lock;
    struct timer **heap;
    struct timer  *list;
    struct timer  *running;
    struct list_head inactive;
} __cacheline_aligned;

static DEFINE_PER_CPU(struct timers, timers);

/* Protects lock-free access to per-timer cpu field against cpu offlining. */
static DEFINE_RCU_READ_LOCK(timer_cpu_read_lock);

DEFINE_PER_CPU(s_time_t, timer_deadline);

/****************************************************************************
 * HEAP OPERATIONS.
 *
 * Slot 0 of the heap is never a valid timer pointer, and instead holds the
 * heap metadata.
 */

struct heap_metadata {
    uint16_t size, limit;
};

static struct heap_metadata *heap_metadata(struct timer **heap)
{
    /* Check that our type-punning doesn't overflow into heap[1] */
    BUILD_BUG_ON(sizeof(struct heap_metadata) > sizeof(struct timer *));

    return (struct heap_metadata *)&heap[0];
}

/* Sink down element @pos of @heap. */
static void down_heap(struct timer **heap, unsigned int pos)
{
    unsigned int sz = heap_metadata(heap)->size, nxt;
    struct timer *t = heap[pos];

    while ( (nxt = (pos << 1)) <= sz )
    {
        if ( ((nxt+1) <= sz) && (heap[nxt+1]->expires < heap[nxt]->expires) )
            nxt++;
        if ( heap[nxt]->expires > t->expires )
            break;
        heap[pos] = heap[nxt];
        heap[pos]->heap_offset = pos;
        pos = nxt;
    }

    heap[pos] = t;
    t->heap_offset = pos;
}

/* Float element @pos up @heap. */
static void up_heap(struct timer **heap, unsigned int pos)
{
    struct timer *t = heap[pos];

    while ( (pos > 1) && (t->expires < heap[pos>>1]->expires) )
    {
        heap[pos] = heap[pos>>1];
        heap[pos]->heap_offset = pos;
        pos >>= 1;
    }

    heap[pos] = t;
    t->heap_offset = pos;
}


/* Delete @t from @heap. Return TRUE if new top of heap. */
static int remove_from_heap(struct timer **heap, struct timer *t)
{
    unsigned int sz = heap_metadata(heap)->size;
    unsigned int pos = t->heap_offset;

    if ( unlikely(pos == sz) )
    {
        heap_metadata(heap)->size = sz - 1;
        goto out;
    }

    heap[pos] = heap[sz];
    heap[pos]->heap_offset = pos;

    heap_metadata(heap)->size = --sz;

    if ( (pos > 1) && (heap[pos]->expires < heap[pos>>1]->expires) )
        up_heap(heap, pos);
    else
        down_heap(heap, pos);

 out:
    return (pos == 1);
}


/* Add new entry @t to @heap. Return TRUE if new top of heap. */
static int add_to_heap(struct timer **heap, struct timer *t)
{
    unsigned int sz = heap_metadata(heap)->size;

    /* Fail if the heap is full. */
    if ( unlikely(sz == heap_metadata(heap)->limit) )
        return 0;

    heap_metadata(heap)->size = ++sz;
    heap[sz] = t;
    t->heap_offset = sz;
    up_heap(heap, sz);

    return (t->heap_offset == 1);
}


/****************************************************************************
 * LINKED LIST OPERATIONS.
 */

static int remove_from_list(struct timer **pprev, struct timer *t)
{
    struct timer *curr, **_pprev = pprev;

    while ( (curr = *_pprev) != t )
        _pprev = &curr->list_next;

    *_pprev = t->list_next;

    return (_pprev == pprev);
}

static int add_to_list(struct timer **pprev, struct timer *t)
{
    struct timer *curr, **_pprev = pprev;

    while ( ((curr = *_pprev) != NULL) && (curr->expires <= t->expires) )
        _pprev = &curr->list_next;

    t->list_next = curr;
    *_pprev = t;

    return (_pprev == pprev);
}


/****************************************************************************
 * TIMER OPERATIONS.
 */

static int remove_entry(struct timer *t)
{
    struct timers *timers = &per_cpu(timers, t->cpu);
    int rc;

    switch ( t->status )
    {
    case TIMER_STATUS_in_heap:
        rc = remove_from_heap(timers->heap, t);
        break;
    case TIMER_STATUS_in_list:
        rc = remove_from_list(&timers->list, t);
        break;
    default:
        rc = 0;
        BUG();
    }

    t->status = TIMER_STATUS_invalid;
    return rc;
}

static int add_entry(struct timer *t)
{
    struct timers *timers = &per_cpu(timers, t->cpu);
    int rc;

    ASSERT(t->status == TIMER_STATUS_invalid);

    /* Try to add to heap. t->heap_offset indicates whether we succeed. */
    t->heap_offset = 0;
    t->status = TIMER_STATUS_in_heap;
    rc = add_to_heap(timers->heap, t);
    if ( t->heap_offset != 0 )
        return rc;

    /* Fall back to adding to the slower linked list. */
    t->status = TIMER_STATUS_in_list;
    return add_to_list(&timers->list, t);
}

static inline void activate_timer(struct timer *timer)
{
    ASSERT(timer->status == TIMER_STATUS_inactive);
    timer->status = TIMER_STATUS_invalid;
    list_del(&timer->inactive);

    if ( add_entry(timer) )
        cpu_raise_softirq(timer->cpu, TIMER_SOFTIRQ);
}

static inline void deactivate_timer(struct timer *timer)
{
    if ( remove_entry(timer) )
        cpu_raise_softirq(timer->cpu, TIMER_SOFTIRQ);

    timer->status = TIMER_STATUS_inactive;
    list_add(&timer->inactive, &per_cpu(timers, timer->cpu).inactive);
}

static inline bool timer_lock_unsafe(struct timer *timer)
{
    unsigned int cpu;

    rcu_read_lock(&timer_cpu_read_lock);

    for ( ; ; )
    {
        cpu = read_atomic(&timer->cpu);
        if ( unlikely(cpu == TIMER_CPU_status_killed) )
        {
            rcu_read_unlock(&timer_cpu_read_lock);
            return 0;
        }
        /* Use the speculation unsafe variant, the wrapper has the barrier. */
        _spin_lock(&per_cpu(timers, cpu).lock);
        if ( likely(timer->cpu == cpu) )
            break;
        spin_unlock(&per_cpu(timers, cpu).lock);
    }

    rcu_read_unlock(&timer_cpu_read_lock);
    return 1;
}

#define timer_lock_irqsave(t, flags) ({         \
    bool __x;                                   \
    local_irq_save(flags);                      \
    if ( !(__x = timer_lock_unsafe(t)) )        \
        local_irq_restore(flags);               \
    block_lock_speculation();                   \
    __x;                                        \
})

static inline void timer_unlock(struct timer *timer)
{
    spin_unlock(&per_cpu(timers, timer->cpu).lock);
}

#define timer_unlock_irqrestore(t, flags) ({    \
    timer_unlock(t);                            \
    local_irq_restore(flags);                   \
})


static bool active_timer(const struct timer *timer)
{
    ASSERT(timer->status >= TIMER_STATUS_inactive);
    return timer_is_active(timer);
}


void init_timer(
    struct timer *timer,
    void        (*function)(void *data),
    void         *data,
    unsigned int  cpu)
{
    unsigned long flags;
    memset(timer, 0, sizeof(*timer));
    timer->function = function;
    timer->data = data;
    write_atomic(&timer->cpu, cpu);
    timer->status = TIMER_STATUS_inactive;
    if ( !timer_lock_irqsave(timer, flags) )
        BUG();
    list_add(&timer->inactive, &per_cpu(timers, cpu).inactive);
    timer_unlock_irqrestore(timer, flags);
}


void set_timer(struct timer *timer, s_time_t expires)
{
    unsigned long flags;

    if ( !timer_lock_irqsave(timer, flags) )
        return;

    if ( active_timer(timer) )
        deactivate_timer(timer);

    timer->expires = expires;

    activate_timer(timer);

    timer_unlock_irqrestore(timer, flags);
}


void stop_timer(struct timer *timer)
{
    unsigned long flags;

    if ( !timer_lock_irqsave(timer, flags) )
        return;

    if ( active_timer(timer) )
        deactivate_timer(timer);

    timer_unlock_irqrestore(timer, flags);
}

bool timer_expires_before(struct timer *timer, s_time_t t)
{
    unsigned long flags;
    bool ret;

    if ( !timer_lock_irqsave(timer, flags) )
        return false;

    ret = active_timer(timer) && timer->expires <= t;

    timer_unlock_irqrestore(timer, flags);

    return ret;
}

void migrate_timer(struct timer *timer, unsigned int new_cpu)
{
    unsigned int old_cpu;
#if CONFIG_NR_CPUS > 1
    bool active;
    unsigned long flags;

    rcu_read_lock(&timer_cpu_read_lock);

    for ( ; ; )
    {
        old_cpu = read_atomic(&timer->cpu);
        if ( (old_cpu == new_cpu) || (old_cpu == TIMER_CPU_status_killed) )
        {
            rcu_read_unlock(&timer_cpu_read_lock);
            return;
        }

        if ( old_cpu < new_cpu )
        {
            spin_lock_irqsave(&per_cpu(timers, old_cpu).lock, flags);
            spin_lock(&per_cpu(timers, new_cpu).lock);
        }
        else
        {
            spin_lock_irqsave(&per_cpu(timers, new_cpu).lock, flags);
            spin_lock(&per_cpu(timers, old_cpu).lock);
        }

        if ( likely(timer->cpu == old_cpu) )
             break;

        spin_unlock(&per_cpu(timers, old_cpu).lock);
        spin_unlock_irqrestore(&per_cpu(timers, new_cpu).lock, flags);
    }

    rcu_read_unlock(&timer_cpu_read_lock);

    active = active_timer(timer);
    if ( active )
        deactivate_timer(timer);

    list_del(&timer->inactive);
    write_atomic(&timer->cpu, new_cpu);
    list_add(&timer->inactive, &per_cpu(timers, new_cpu).inactive);

    if ( active )
        activate_timer(timer);

    spin_unlock(&per_cpu(timers, old_cpu).lock);
    spin_unlock_irqrestore(&per_cpu(timers, new_cpu).lock, flags);
#else /* CONFIG_NR_CPUS == 1 */
    old_cpu = read_atomic(&timer->cpu);
    if ( old_cpu != TIMER_CPU_status_killed )
        WARN_ON(new_cpu != old_cpu);
#endif /* CONFIG_NR_CPUS */
}


void kill_timer(struct timer *timer)
{
    unsigned int old_cpu, cpu;
    unsigned long flags;

    BUG_ON(this_cpu(timers).running == timer);

    if ( !timer_lock_irqsave(timer, flags) )
        return;

    if ( active_timer(timer) )
        deactivate_timer(timer);

    list_del(&timer->inactive);
    timer->status = TIMER_STATUS_killed;
    old_cpu = timer->cpu;
    write_atomic(&timer->cpu, TIMER_CPU_status_killed);

    spin_unlock_irqrestore(&per_cpu(timers, old_cpu).lock, flags);

    for_each_online_cpu ( cpu )
        while ( per_cpu(timers, cpu).running == timer )
            cpu_relax();
}


static void execute_timer(struct timers *ts, struct timer *t)
{
    void (*fn)(void *data) = t->function;
    void *data = t->data;

    t->status = TIMER_STATUS_inactive;
    list_add(&t->inactive, &ts->inactive);

    ts->running = t;
    spin_unlock_irq(&ts->lock);
    (*fn)(data);
    spin_lock_irq(&ts->lock);
    ts->running = NULL;
}


static void cf_check timer_softirq_action(void)
{
    struct timer  *t, **heap, *next;
    struct timers *ts;
    s_time_t       now, deadline;

    ts = &this_cpu(timers);
    heap = ts->heap;

    /* If we overflowed the heap, try to allocate a larger heap. */
    if ( unlikely(ts->list != NULL) )
    {
        /* old_limit == (2^n)-1; new_limit == (2^(n+4))-1 */
        unsigned int old_limit = heap_metadata(heap)->limit;
        unsigned int new_limit = ((old_limit + 1) << 4) - 1;
        struct timer **newheap = NULL;

        /* Don't grow the heap beyond what is representable in its metadata. */
        if ( new_limit == (typeof(heap_metadata(heap)->limit))new_limit &&
             new_limit + 1 )
            newheap = xmalloc_array(struct timer *, new_limit + 1);
        else
            printk_once(PRTOSLOG_WARNING "CPU%u: timer heap limit reached\n",
                        smp_processor_id());
        if ( newheap != NULL )
        {
            spin_lock_irq(&ts->lock);
            memcpy(newheap, heap, (old_limit + 1) * sizeof(*heap));
            heap_metadata(newheap)->limit = new_limit;
            ts->heap = newheap;
            spin_unlock_irq(&ts->lock);
            if ( old_limit != 0 )
                xfree(heap);
            heap = newheap;
        }
    }

    spin_lock_irq(&ts->lock);

    now = NOW();

    /* Execute ready heap timers. */
    while ( (heap_metadata(heap)->size != 0) &&
            ((t = heap[1])->expires < now) )
    {
        remove_from_heap(heap, t);
        execute_timer(ts, t);
    }

    /* Execute ready list timers. */
    while ( ((t = ts->list) != NULL) && (t->expires < now) )
    {
        ts->list = t->list_next;
        execute_timer(ts, t);
    }

    /* Try to move timers from linked list to more efficient heap. */
    next = ts->list;
    ts->list = NULL;
    while ( unlikely((t = next) != NULL) )
    {
        next = t->list_next;
        t->status = TIMER_STATUS_invalid;
        add_entry(t);
    }

    /* Find earliest deadline from head of linked list and top of heap. */
    deadline = STIME_MAX;
    if ( heap_metadata(heap)->size != 0 )
        deadline = heap[1]->expires;
    if ( (ts->list != NULL) && (ts->list->expires < deadline) )
        deadline = ts->list->expires;
    now = NOW();
    this_cpu(timer_deadline) =
        (deadline == STIME_MAX) ? 0 : MAX(deadline, now + timer_slop);

    if ( !reprogram_timer(this_cpu(timer_deadline)) )
        raise_softirq(TIMER_SOFTIRQ);

    spin_unlock_irq(&ts->lock);
}


static void dump_timer(struct timer *t, s_time_t now)
{
    printk("  ex=%12"PRId64"us timer=%p cb=%ps(%p)\n",
           (t->expires - now) / 1000, t, t->function, t->data);
}

static void cf_check dump_timerq(unsigned char key)
{
    struct timer  *t;
    struct timers *ts;
    unsigned long  flags;
    s_time_t       now = NOW();
    unsigned int   i, j;

    printk("Dumping timer queues:\n");

    for_each_online_cpu( i )
    {
        ts = &per_cpu(timers, i);

        printk("CPU%02d:\n", i);
        spin_lock_irqsave(&ts->lock, flags);
        for ( j = 1; j <= heap_metadata(ts->heap)->size; j++ )
            dump_timer(ts->heap[j], now);
        for ( t = ts->list; t != NULL; t = t->list_next )
            dump_timer(t, now);
        spin_unlock_irqrestore(&ts->lock, flags);
    }
}

static void migrate_timers_from_cpu(unsigned int old_cpu)
{
    unsigned int new_cpu = cpumask_any(&cpu_online_map);
    struct timers *old_ts, *new_ts;
    struct timer *t;
    bool notify = false;

    ASSERT(!cpu_online(old_cpu) && cpu_online(new_cpu));

    old_ts = &per_cpu(timers, old_cpu);
    new_ts = &per_cpu(timers, new_cpu);

    if ( old_cpu < new_cpu )
    {
        spin_lock_irq(&old_ts->lock);
        spin_lock(&new_ts->lock);
    }
    else
    {
        spin_lock_irq(&new_ts->lock);
        spin_lock(&old_ts->lock);
    }

    while ( (t = heap_metadata(old_ts->heap)->size
             ? old_ts->heap[1] : old_ts->list) != NULL )
    {
        remove_entry(t);
        write_atomic(&t->cpu, new_cpu);
        notify |= add_entry(t);
    }

    while ( !list_empty(&old_ts->inactive) )
    {
        t = list_entry(old_ts->inactive.next, struct timer, inactive);
        list_del(&t->inactive);
        write_atomic(&t->cpu, new_cpu);
        list_add(&t->inactive, &new_ts->inactive);
    }

    spin_unlock(&old_ts->lock);
    spin_unlock_irq(&new_ts->lock);

    if ( notify )
        cpu_raise_softirq(new_cpu, TIMER_SOFTIRQ);
}

/*
 * All CPUs initially share an empty dummy heap. Only those CPUs that
 * are brought online will be dynamically allocated their own heap.
 * The size/limit metadata are both 0 by being in .bss
 */
static struct timer *dummy_heap[1];

static void free_percpu_timers(unsigned int cpu)
{
    struct timers *ts = &per_cpu(timers, cpu);

    ASSERT(heap_metadata(ts->heap)->size == 0);
    if ( heap_metadata(ts->heap)->limit )
    {
        xfree(ts->heap);
        ts->heap = dummy_heap;
    }
    else
        ASSERT(ts->heap == dummy_heap);
}

static int cf_check cpu_callback(
    struct notifier_block *nfb, unsigned long action, void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;
    struct timers *ts = &per_cpu(timers, cpu);

    switch ( action )
    {
    case CPU_UP_PREPARE:
        /* Only initialise ts once. */
        if ( !ts->heap )
        {
            INIT_LIST_HEAD(&ts->inactive);
            spin_lock_init(&ts->lock);
            ts->heap = dummy_heap;
        }
        break;

    case CPU_UP_CANCELED:
    case CPU_DEAD:
    case CPU_RESUME_FAILED:
        migrate_timers_from_cpu(cpu);

        if ( !park_offline_cpus && system_state != SYS_STATE_suspend )
            free_percpu_timers(cpu);
        break;

    case CPU_REMOVE:
        if ( park_offline_cpus )
            free_percpu_timers(cpu);
        break;

    default:
        break;
    }

    return NOTIFY_DONE;
}

void init_timers_prtos(unsigned int cpu) {
    struct timers *ts = &per_cpu(timers, cpu);
    /* Only initialise ts once. */
    if (!ts->heap) {
        INIT_LIST_HEAD(&ts->inactive);
        spin_lock_init(&ts->lock);
        ts->heap = dummy_heap;
    }
}

static struct notifier_block cpu_nfb = {
    .notifier_call = cpu_callback,
    .priority = 99
};

void __init timer_init(void)
{
    void *cpu = (void *)(long)smp_processor_id();

    open_softirq(TIMER_SOFTIRQ, timer_softirq_action);

    cpu_callback(&cpu_nfb, CPU_UP_PREPARE, cpu);
    register_cpu_notifier(&cpu_nfb);

    register_keyhandler('a', dump_timerq, "dump timer queues", 1);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: timer.c === */
/* === BEGIN INLINED: vtimer.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/vtimer.c
 *
 * ARM Virtual Timer emulation support
 *
 * Ian Campbell <ian.campbell@citrix.com>
 * Copyright (c) 2011 Citrix Systems.
 */

#include <prtos_acpi.h>
#include <prtos_lib.h>
#include <prtos_perfc.h>
#include <prtos_sched.h>
#include <prtos_timer.h>

#include <asm_cpregs.h>
#include <asm_div64.h>
#include <asm_irq.h>
#include <asm_regs.h>
#include <asm_time.h>
#include <asm_vgic.h>
#include <asm_vreg.h>
#include <asm_vtimer.h>

/*
 * Check if regs is allowed access, user_gate is tail end of a
 * CNTKCTL_EL1_ bit name which gates user access
 */
#define ACCESS_ALLOWED(regs, user_gate) \
    ( !regs_mode_is_user(regs) || \
      (READ_SYSREG(CNTKCTL_EL1) & CNTKCTL_EL1_##user_gate) )

static void phys_timer_expired(void *data)
{
    struct vtimer *t = data;
    t->ctl |= CNTx_CTL_PENDING;
    if ( !(t->ctl & CNTx_CTL_MASK) )
    {
        perfc_incr(vtimer_phys_inject);
        vgic_inject_irq(t->v->domain, t->v, t->irq, true);
    }
    else
        perfc_incr(vtimer_phys_masked);
}

static void virt_timer_expired(void *data)
{
    struct vtimer *t = data;
    t->ctl |= CNTx_CTL_MASK;
    vgic_inject_irq(t->v->domain, t->v, t->irq, true);
    perfc_incr(vtimer_virt_inject);
}

int domain_vtimer_init(struct domain *d, struct prtos_arch_domainconfig *config)
{
    d->arch.virt_timer_base.offset = get_cycles();
    d->arch.virt_timer_base.nanoseconds =
        ticks_to_ns(d->arch.virt_timer_base.offset - boot_count);
    d->time_offset.seconds = d->arch.virt_timer_base.nanoseconds;
    do_div(d->time_offset.seconds, 1000000000);

    config->clock_frequency = timer_dt_clock_frequency;

    /*
     * Per the ACPI specification, providing a secure EL1 timer
     * interrupt is optional and will be ignored by non-secure OS.
     * Therefore don't reserve the interrupt number for the HW domain
     * and ACPI.
     *
     * Note that we should still reserve it when using the Device-Tree
     * because the interrupt is not optional. That said, we are not
     * expecting any OS to use it when running on top of PRTOS.
     *
     * At this stage vgic_reserve_virq() is not meant to fail.
     */
    if ( is_hardware_domain(d) )
    {
        if ( acpi_disabled &&
             !vgic_reserve_virq(d, timer_get_irq(TIMER_PHYS_SECURE_PPI)) )
            BUG();

        if ( !vgic_reserve_virq(d, timer_get_irq(TIMER_PHYS_NONSECURE_PPI)) )
            BUG();

        if ( !vgic_reserve_virq(d, timer_get_irq(TIMER_VIRT_PPI)) )
            BUG();
    }
    else
    {
        if ( !vgic_reserve_virq(d, GUEST_TIMER_PHYS_S_PPI) )
            BUG();

        if ( !vgic_reserve_virq(d, GUEST_TIMER_PHYS_NS_PPI) )
            BUG();

        if ( !vgic_reserve_virq(d, GUEST_TIMER_VIRT_PPI) )
            BUG();
    }

    return 0;
}

int vcpu_vtimer_init(struct vcpu *v)
{
    struct vtimer *t = &v->arch.phys_timer;
    bool d0 = is_hardware_domain(v->domain);

    /*
     * Hardware domain uses the hardware interrupts, guests get the virtual
     * platform.
     */

    init_timer(&t->timer, phys_timer_expired, t, v->processor);
    t->ctl = 0;
    t->irq = d0
        ? timer_get_irq(TIMER_PHYS_NONSECURE_PPI)
        : GUEST_TIMER_PHYS_NS_PPI;
    t->v = v;

    t = &v->arch.virt_timer;
    init_timer(&t->timer, virt_timer_expired, t, v->processor);
    t->ctl = 0;
    t->irq = d0
        ? timer_get_irq(TIMER_VIRT_PPI)
        : GUEST_TIMER_VIRT_PPI;
    t->v = v;

    v->arch.vtimer_initialized = 1;

    return 0;
}

void vcpu_timer_destroy(struct vcpu *v)
{
    if ( !v->arch.vtimer_initialized )
        return;

    kill_timer(&v->arch.virt_timer.timer);
    kill_timer(&v->arch.phys_timer.timer);
}

void virt_timer_save(struct vcpu *v)
{
    ASSERT(!is_idle_vcpu(v));

    v->arch.virt_timer.ctl = READ_SYSREG(CNTV_CTL_EL0);
    WRITE_SYSREG(v->arch.virt_timer.ctl & ~CNTx_CTL_ENABLE, CNTV_CTL_EL0);
    v->arch.virt_timer.cval = READ_SYSREG64(CNTV_CVAL_EL0);
    if ( (v->arch.virt_timer.ctl & CNTx_CTL_ENABLE) &&
         !(v->arch.virt_timer.ctl & CNTx_CTL_MASK))
    {
        set_timer(&v->arch.virt_timer.timer,
                  v->domain->arch.virt_timer_base.nanoseconds +
                  ticks_to_ns(v->arch.virt_timer.cval));
    }
}

void virt_timer_restore(struct vcpu *v)
{
    ASSERT(!is_idle_vcpu(v));

    stop_timer(&v->arch.virt_timer.timer);
    migrate_timer(&v->arch.virt_timer.timer, v->processor);
    migrate_timer(&v->arch.phys_timer.timer, v->processor);

    WRITE_SYSREG64(v->domain->arch.virt_timer_base.offset, CNTVOFF_EL2);
    WRITE_SYSREG64(v->arch.virt_timer.cval, CNTV_CVAL_EL0);
    WRITE_SYSREG(v->arch.virt_timer.ctl, CNTV_CTL_EL0);
}

static bool vtimer_cntp_ctl(struct cpu_user_regs *regs, register_t *r,
                            bool read)
{
    struct vcpu *v = current;
    s_time_t expires;

    if ( !ACCESS_ALLOWED(regs, EL0PTEN) )
        return false;

    if ( read )
    {
        *r = v->arch.phys_timer.ctl;
    }
    else
    {
        uint32_t ctl = *r & ~CNTx_CTL_PENDING;
        if ( ctl & CNTx_CTL_ENABLE )
            ctl |= v->arch.phys_timer.ctl & CNTx_CTL_PENDING;
        v->arch.phys_timer.ctl = ctl;

        if ( v->arch.phys_timer.ctl & CNTx_CTL_ENABLE )
        {
            /*
             * If cval is before the point PRTOS started, expire timer
             * immediately.
             */
            expires = v->arch.phys_timer.cval > boot_count
                      ? ticks_to_ns(v->arch.phys_timer.cval - boot_count) : 0;
            set_timer(&v->arch.phys_timer.timer, expires);
        }
        else
            stop_timer(&v->arch.phys_timer.timer);
    }
    return true;
}

static bool vtimer_cntp_tval(struct cpu_user_regs *regs, register_t *r,
                             bool read)
{
    struct vcpu *v = current;
    uint64_t cntpct;
    s_time_t expires;

    if ( !ACCESS_ALLOWED(regs, EL0PTEN) )
        return false;

    cntpct = get_cycles();

    if ( read )
    {
        *r = (uint32_t)((v->arch.phys_timer.cval - cntpct) & 0xffffffffULL);
    }
    else
    {
        v->arch.phys_timer.cval = cntpct + (uint64_t)(int32_t)*r;
        if ( v->arch.phys_timer.ctl & CNTx_CTL_ENABLE )
        {
            v->arch.phys_timer.ctl &= ~CNTx_CTL_PENDING;
            /*
             * If cval is before the point PRTOS started, expire timer
             * immediately.
             */
            expires = v->arch.phys_timer.cval > boot_count
                      ? ticks_to_ns(v->arch.phys_timer.cval - boot_count) : 0;
            set_timer(&v->arch.phys_timer.timer, expires);
        }
    }
    return true;
}

static bool vtimer_cntp_cval(struct cpu_user_regs *regs, uint64_t *r,
                             bool read)
{
    struct vcpu *v = current;
    s_time_t expires;

    if ( !ACCESS_ALLOWED(regs, EL0PTEN) )
        return false;

    if ( read )
    {
        *r = v->arch.phys_timer.cval;
    }
    else
    {
        v->arch.phys_timer.cval = *r;
        if ( v->arch.phys_timer.ctl & CNTx_CTL_ENABLE )
        {
            v->arch.phys_timer.ctl &= ~CNTx_CTL_PENDING;
            /*
             * If cval is before the point PRTOS started, expire timer
             * immediately.
             */
            expires = v->arch.phys_timer.cval > boot_count
                      ? ticks_to_ns(v->arch.phys_timer.cval - boot_count) : 0;
            set_timer(&v->arch.phys_timer.timer, expires);
        }
    }
    return true;
}

static bool vtimer_emulate_cp32(struct cpu_user_regs *regs, union hsr hsr)
{
    struct hsr_cp32 cp32 = hsr.cp32;

    if ( cp32.read )
        perfc_incr(vtimer_cp32_reads);
    else
        perfc_incr(vtimer_cp32_writes);

    switch ( hsr.bits & HSR_CP32_REGS_MASK )
    {
    case HSR_CPREG32(CNTP_CTL):
        return vreg_emulate_cp32(regs, hsr, vtimer_cntp_ctl);

    case HSR_CPREG32(CNTP_TVAL):
        return vreg_emulate_cp32(regs, hsr, vtimer_cntp_tval);

    default:
        return false;
    }
}

static bool vtimer_emulate_cp64(struct cpu_user_regs *regs, union hsr hsr)
{
    struct hsr_cp64 cp64 = hsr.cp64;

    if ( cp64.read )
        perfc_incr(vtimer_cp64_reads);
    else
        perfc_incr(vtimer_cp64_writes);

    switch ( hsr.bits & HSR_CP64_REGS_MASK )
    {
    case HSR_CPREG64(CNTP_CVAL):
        return vreg_emulate_cp64(regs, hsr, vtimer_cntp_cval);

    default:
        return false;
    }
}

#ifdef CONFIG_ARM_64
static bool vtimer_emulate_sysreg(struct cpu_user_regs *regs, union hsr hsr)
{
    struct hsr_sysreg sysreg = hsr.sysreg;

    if ( sysreg.read )
        perfc_incr(vtimer_sysreg_reads);
    else
        perfc_incr(vtimer_sysreg_writes);

    switch ( hsr.bits & HSR_SYSREG_REGS_MASK )
    {
    case HSR_SYSREG_CNTP_CTL_EL0:
        return vreg_emulate_sysreg(regs, hsr, vtimer_cntp_ctl);
    case HSR_SYSREG_CNTP_TVAL_EL0:
        return vreg_emulate_sysreg(regs, hsr, vtimer_cntp_tval);
    case HSR_SYSREG_CNTP_CVAL_EL0:
        return vreg_emulate_sysreg(regs, hsr, vtimer_cntp_cval);

    default:
        return false;
    }

}
#endif

bool vtimer_emulate(struct cpu_user_regs *regs, union hsr hsr)
{

    switch (hsr.ec) {
    case HSR_EC_CP15_32:
        return vtimer_emulate_cp32(regs, hsr);
    case HSR_EC_CP15_64:
        return vtimer_emulate_cp64(regs, hsr);
#ifdef CONFIG_ARM_64
    case HSR_EC_SYSREG:
        return vtimer_emulate_sysreg(regs, hsr);
#endif
    default:
        return false;
    }
}


/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: vtimer.c === */
/* === BEGIN INLINED: common_time.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * time.c
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <prtos_sched.h>
#include <prtos_shared.h>
#include <prtos_spinlock.h>
#include <prtos_time.h>
#include <asm_div64.h>
#include <asm_domain.h>

/* Nonzero if YEAR is a leap year (every 4 years,
   except every 100th isn't, and every 400th is).  */
#define __isleap(year) \
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

/* How many days are in each month.  */
static const unsigned short int __mon_lengths[2][12] = {
    /* Normal years.  */
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    /* Leap years.  */
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

#define SECS_PER_HOUR (60 * 60)
#define SECS_PER_DAY  (SECS_PER_HOUR * 24)

static uint64_t wc_sec; /* UTC time at last 'time update'. */
static unsigned int wc_nsec;
static DEFINE_SPINLOCK(wc_lock);

struct tm gmtime(unsigned long t)
{
    struct tm tbuf;
    long days, rem;
    int y;
    const unsigned short int *ip;

    y = 1970;
#if BITS_PER_LONG >= 64
    /* Allow the concept of time before 1970.  64-bit only; for 32-bit
     * time after 2038 seems more important than time before 1970. */
    while ( t & (1UL<<39) )
    {
        y -= 400;
        t += ((unsigned long)(365 * 303 + 366 * 97)) * SECS_PER_DAY;
    }
    t &= (1UL << 40) - 1;
#endif

    days = t / SECS_PER_DAY;
    rem = t % SECS_PER_DAY;

    tbuf.tm_hour = rem / SECS_PER_HOUR;
    rem %= SECS_PER_HOUR;
    tbuf.tm_min = rem / 60;
    tbuf.tm_sec = rem % 60;
    /* January 1, 1970 was a Thursday.  */
    tbuf.tm_wday = (4 + days) % 7;
    if ( tbuf.tm_wday < 0 )
        tbuf.tm_wday += 7;
    while ( days >= (rem = __isleap(y) ? 366 : 365) )
    {
        ++y;
        days -= rem;
    }
    while ( days < 0 )
    {
        --y;
        days += __isleap(y) ? 366 : 365;
    }
    tbuf.tm_year = y - 1900;
    tbuf.tm_yday = days;
    ip = (const unsigned short int *)__mon_lengths[__isleap(y)];
    for ( y = 0; days >= ip[y]; ++y )
        days -= ip[y];
    tbuf.tm_mon = y;
    tbuf.tm_mday = days + 1;
    tbuf.tm_isdst = -1;

    return tbuf;
}

void update_domain_wallclock_time(struct domain *d)
{
    uint32_t *wc_version;
    uint64_t sec;

    spin_lock(&wc_lock);

    wc_version = &shared_info(d, wc_version);
    *wc_version = version_update_begin(*wc_version);
    smp_wmb();

    sec = wc_sec + d->time_offset.seconds;
    shared_info(d, wc_sec)    = sec;
    shared_info(d, wc_nsec)   = wc_nsec;
#if defined(CONFIG_X86) && defined(CONFIG_COMPAT)
    if ( likely(!has_32bit_shinfo(d)) )
        d->shared_info->native.wc_sec_hi = sec >> 32;
    else
        d->shared_info->compat.arch.wc_sec_hi = sec >> 32;
#else
    shared_info(d, wc_sec_hi) = sec >> 32;
#endif

    smp_wmb();
    *wc_version = version_update_end(*wc_version);

    spin_unlock(&wc_lock);
}

/* Set clock to <secs,usecs> after 00:00:00 UTC, 1 January, 1970. */
void do_settime(u64 secs, unsigned int nsecs, u64 system_time_base)
{
    u64 x;
    u32 y;
    struct domain *d;

    x = SECONDS(secs) + nsecs - system_time_base;
    y = do_div(x, 1000000000);

    spin_lock(&wc_lock);
    wc_sec  = x;
    wc_nsec = y;
    spin_unlock(&wc_lock);

    rcu_read_lock(&domlist_read_lock);
    for_each_domain ( d )
        update_domain_wallclock_time(d);
    rcu_read_unlock(&domlist_read_lock);
}




struct tm wallclock_time(uint64_t *ns)
{
    uint64_t seconds, nsec;

    if ( !wc_sec )
        return (struct tm) { 0 };

    seconds = NOW() + SECONDS(wc_sec) + wc_nsec;
    nsec = do_div(seconds, 1000000000);

    if ( ns )
        *ns = nsec;

    return gmtime(seconds);
}

/* === END INLINED: common_time.c === */
