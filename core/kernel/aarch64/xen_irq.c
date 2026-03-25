/* Xen interrupt & GIC - consolidated */
/* === BEGIN INLINED: gic.c === */
#include <xen_xen_config.h>
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/gic.c
 *
 * ARM Generic Interrupt Controller support
 *
 * Tim Deegan <tim@xen.org>
 * Copyright (c) 2011 Citrix Systems.
 */

#include <xen_xen_config.h>

#include <xen_lib.h>
#include <xen_init.h>
#include <xen_mm.h>
#include <xen_irq.h>
#include <xen_sched.h>
#include <xen_errno.h>
#include <xen_softirq.h>
#include <xen_list.h>
#include <xen_device_tree.h>
#include <xen_acpi.h>
#include <xen_cpu.h>
#include <xen_notifier.h>
#include <asm_p2m.h>
#include <asm_domain.h>
#include <asm_platform.h>
#include <asm_generic_device.h>
#include <asm_io.h>
#include <asm_gic.h>
#include <asm_vgic.h>
#include <asm_acpi.h>

DEFINE_PER_CPU(uint64_t, lr_mask);

#undef GIC_DEBUG

const struct gic_hw_operations *gic_hw_ops;

static void __init __maybe_unused build_assertions(void)
{
    /* Check our enum gic_sgi only covers SGIs */
    BUILD_BUG_ON(GIC_SGI_STATIC_MAX > NR_GIC_SGI);
}

void register_gic_ops(const struct gic_hw_operations *ops)
{
    gic_hw_ops = ops;
}

static void clear_cpu_lr_mask(void)
{
    this_cpu(lr_mask) = 0ULL;
}

enum gic_version gic_hw_version(void)
{
   return gic_hw_ops->info->hw_version;
}

unsigned int gic_number_lines(void)
{
    return gic_hw_ops->info->nr_lines;
}

void gic_save_state(struct vcpu *v)
{
    ASSERT(!local_irq_is_enabled());
    ASSERT(!is_idle_vcpu(v));

    /* No need for spinlocks here because interrupts are disabled around
     * this call and it only accesses struct vcpu fields that cannot be
     * accessed simultaneously by another pCPU.
     */
    v->arch.lr_mask = this_cpu(lr_mask);
    gic_hw_ops->save_state(v);
    isb();
}

void gic_restore_state(struct vcpu *v)
{
    ASSERT(!local_irq_is_enabled());
    ASSERT(!is_idle_vcpu(v));

    this_cpu(lr_mask) = v->arch.lr_mask;
    gic_hw_ops->restore_state(v);

    isb();
}

/* desc->irq needs to be disabled before calling this function */
void gic_set_irq_type(struct irq_desc *desc, unsigned int type)
{
    /*
     * IRQ must be disabled before configuring it (see 4.3.13 in ARM IHI
     * 0048B.b). We rely on the caller to do it.
     */
    ASSERT(test_bit(_IRQ_DISABLED, &desc->status));
    ASSERT(spin_is_locked(&desc->lock));
    // ASSERT(type != IRQ_TYPE_INVALID); // WA for this line

    gic_hw_ops->set_irq_type(desc, type);
}

static void gic_set_irq_priority(struct irq_desc *desc, unsigned int priority)
{
    gic_hw_ops->set_irq_priority(desc, priority);
}

/* Program the GIC to route an interrupt to the host (i.e. Xen)
 * - needs to be called with desc.lock held
 */
void gic_route_irq_to_xen(struct irq_desc *desc, unsigned int priority)
{
    ASSERT(priority <= 0xff);     /* Only 8 bits of priority */
    ASSERT(desc->irq < gic_number_lines());/* Can't route interrupts that don't exist */
    ASSERT(test_bit(_IRQ_DISABLED, &desc->status));
    ASSERT(spin_is_locked(&desc->lock));

    desc->handler = gic_hw_ops->gic_host_irq_type;

    /* SGIs are always edge-triggered, so there is need to set it */
    if ( desc->irq >= NR_GIC_SGI)
        gic_set_irq_type(desc, desc->arch.type);
    gic_set_irq_priority(desc, priority);
}

/* Program the GIC to route an interrupt to a guest
 *   - desc.lock must be held
 */
int gic_route_irq_to_guest(struct domain *d, unsigned int virq,
                           struct irq_desc *desc, unsigned int priority)
{
    int ret;

    ASSERT(spin_is_locked(&desc->lock));
    /* Caller has already checked that the IRQ is an SPI */
    ASSERT(virq >= 32);
    ASSERT(virq < vgic_num_irqs(d));
    ASSERT(!is_lpi(virq));

    ret = vgic_connect_hw_irq(d, NULL, virq, desc, true);
    if ( ret )
        return ret;

    desc->handler = gic_hw_ops->gic_guest_irq_type;
    set_bit(_IRQ_GUEST, &desc->status);

    if ( !irq_type_set_by_domain(d) )
        gic_set_irq_type(desc, desc->arch.type);
    gic_set_irq_priority(desc, priority);

    return 0;
}

/* This function only works with SPIs for now */
int gic_remove_irq_from_guest(struct domain *d, unsigned int virq,
                              struct irq_desc *desc)
{
    int ret;

    ASSERT(spin_is_locked(&desc->lock));
    ASSERT(test_bit(_IRQ_GUEST, &desc->status));
    ASSERT(!is_lpi(virq));

    /*
     * Removing an interrupt while the domain is running may have
     * undesirable effect on the vGIC emulation.
     */
    if ( !d->is_dying )
        return -EBUSY;

    desc->handler->shutdown(desc);

    /* EOI the IRQ if it has not been done by the guest */
    if ( test_bit(_IRQ_INPROGRESS, &desc->status) )
        gic_hw_ops->deactivate_irq(desc);
    clear_bit(_IRQ_INPROGRESS, &desc->status);

    ret = vgic_connect_hw_irq(d, NULL, virq, desc, false);
    if ( ret )
        return ret;

    clear_bit(_IRQ_GUEST, &desc->status);
    desc->handler = &no_irq_type;

    return 0;
}

int gic_irq_xlate(const u32 *intspec, unsigned int intsize,
                  unsigned int *out_hwirq,
                  unsigned int *out_type)
{
    if ( intsize < 3 )
        return -EINVAL;

    /* Get the interrupt number and add 16 to skip over SGIs */
    *out_hwirq = intspec[1] + 16;

    /* For SPIs, we need to add 16 more to get the GIC irq ID number */
    if ( !intspec[0] )
        *out_hwirq += 16;

    if ( out_type )
        *out_type = intspec[2] & IRQ_TYPE_SENSE_MASK;

    return 0;
}

/* Map extra GIC MMIO, irqs and other hw stuffs to the hardware domain. */
int gic_map_hwdom_extra_mappings(struct domain *d)
{
    if ( gic_hw_ops->map_hwdom_extra_mappings )
        return gic_hw_ops->map_hwdom_extra_mappings(d);

    return 0;
}

static void __init gic_dt_preinit(void)
{
    int rc;
    struct dt_device_node *node;
    uint8_t num_gics = 0;

    dt_for_each_device_node( dt_host, node )
    {
        if ( !dt_get_property(node, "interrupt-controller", NULL) )
            continue;

        if ( !dt_get_parent(node) )
            continue;

        rc = device_init(node, DEVICE_INTERRUPT_CONTROLLER, NULL);
        if ( !rc )
        {
            /* NOTE: Only one GIC is supported */
            num_gics = 1;
            break;
        }
    }
    if ( !num_gics )
        panic("Unable to find compatible GIC in the device tree\n");

    /* Set the GIC as the primary interrupt controller */
    dt_interrupt_controller = node;
    dt_device_set_used_by(node, DOMID_XEN);
}

#ifdef CONFIG_ACPI
static void __init gic_acpi_preinit(void)
{
    struct acpi_subtable_header *header;
    struct acpi_madt_generic_distributor *dist;

    header = acpi_table_get_entry_madt(ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR, 0);
    if ( !header )
        panic("No valid GICD entries exists\n");

    dist = container_of(header, struct acpi_madt_generic_distributor, header);

    if ( acpi_device_init(DEVICE_INTERRUPT_CONTROLLER, NULL, dist->version) )
        panic("Unable to find compatible GIC in the ACPI table\n");
}
#else
static void __init gic_acpi_preinit(void) { }
#endif

/* Find the interrupt controller and set up the callback to translate
 * device tree or ACPI IRQ.
 */
void __init gic_preinit(void)
{
    if ( acpi_disabled )
        gic_dt_preinit();
    else
        gic_acpi_preinit();
}

/* Set up the GIC */
void __init gic_init(void)
{
    if ( gic_hw_ops->init() )
        panic("Failed to initialize the GIC drivers\n");
    /* Clear LR mask for cpu0 */
    clear_cpu_lr_mask();
}

void send_SGI_mask(const cpumask_t *cpumask, enum gic_sgi sgi)
{
    gic_hw_ops->send_SGI(sgi, SGI_TARGET_LIST, cpumask);
}

void send_SGI_one(unsigned int cpu, enum gic_sgi sgi)
{
    send_SGI_mask(cpumask_of(cpu), sgi);
}


void send_SGI_allbutself(enum gic_sgi sgi)
{
   gic_hw_ops->send_SGI(sgi, SGI_TARGET_OTHERS, NULL);
}

void smp_send_state_dump(unsigned int cpu)
{
    send_SGI_one(cpu, GIC_SGI_DUMP_STATE);
}

/* Set up the per-CPU parts of the GIC for a secondary CPU */
void gic_init_secondary_cpu(void)
{
    gic_hw_ops->secondary_init();
    /* Clear LR mask for secondary cpus */
    clear_cpu_lr_mask();
}

/* Shut down the per-CPU GIC interface */
void gic_disable_cpu(void)
{
    ASSERT(!local_irq_is_enabled());

    gic_hw_ops->disable_interface();
}

static void do_static_sgi(struct cpu_user_regs *regs, enum gic_sgi sgi)
{
    struct irq_desc *desc = irq_to_desc(sgi);

    perfc_incr(ipis);

    /* Lower the priority */
    gic_hw_ops->eoi_irq(desc);

    /*
     * Ensure any shared data written by the CPU sending
     * the IPI is read after we've read the ACK register on the GIC.
     * Matches the write barrier in send_SGI_* helpers.
     */
    smp_rmb();

    switch (sgi)
    {
    case GIC_SGI_EVENT_CHECK:
        /* Nothing to do, will check for events on return path */
        break;
    case GIC_SGI_DUMP_STATE:
        dump_execstate(regs);
        break;
    case GIC_SGI_CALL_FUNCTION:
        smp_call_function_interrupt();
        break;
    default:
        panic("Unhandled SGI %d on CPU%d\n", sgi, smp_processor_id());
        break;
    }

    /* Deactivate */
    gic_hw_ops->deactivate_irq(desc);
}

/* Accept an interrupt from the GIC and dispatch its handler */
void gic_interrupt(struct cpu_user_regs *regs, int is_fiq)
{
    unsigned int irq;

    do  {
        /* Reading IRQ will ACK it */
        irq = gic_hw_ops->read_irq();

        if ( likely(irq >= GIC_SGI_STATIC_MAX && irq < 1020) )
        {
            isb();
            do_IRQ(regs, irq, is_fiq);
        }
        else if ( is_lpi(irq) )
        {
            isb();
            gic_hw_ops->do_LPI(irq);
        }
        else if ( unlikely(irq < 16) )
        {
            do_static_sgi(regs, irq);
        }
        else
        {
            local_irq_disable();
            break;
        }
    } while (1);
}

#if CONFIG_STATIC_IRQ_ROUTING
extern void static_htimer_isr(int irq);
extern void static_vtimer_isr(int irq);
extern void prtos_gicv3_eoi_irq(int irq);
extern void prtos_gicv3_host_irq_end(int irq);
void static_gic_interrupt(struct cpu_user_regs *regs) {
    unsigned int irq;

    while (1) {
        irq = gic_hw_ops->read_irq(); /* ACK */

        if (irq < 16) {
            do_static_sgi(regs, irq);
        } else if (irq < 32) {
            /* ========== PPI ==========
             *
             * IRQ 26: CNTHP EL2 timer
             * IRQ 27: CNTV virtual timer
             */
            switch (irq) {
                case 26:
                    /*
                     * Hypervisor timer — EOI+DIR is done inside
                     * static_htimer_isr() before the handler may
                     * context-switch.  Use continue to skip the
                     * loop-bottom EOI+DIR.
                     */
                    static_htimer_isr(irq);
                    continue;

                case 27:
                    /* Virtual timer — inject to partition via ICH_LR */
                    static_vtimer_isr(irq);
                    break;

                default:
                    break;
            }
        } else if (irq < 1020) {
            extern int static_handle_spi(struct cpu_user_regs *regs,
                                         unsigned int irq);
            if (static_handle_spi(regs, irq)) {
                /* HW-linked virtual SPI: only EOI, no DIR.
                 * Physical deactivation happens when the guest EOIs
                 * the virtual IRQ via ICH_LR.HW linkage. */
                prtos_gicv3_eoi_irq(irq);
                continue;
            }
        } else {
            local_irq_disable();
            break;
        }

        prtos_gicv3_host_irq_end(irq);
    }

    /* Process deferred hypervisor timer AFTER the GIC loop exits,
     * so schedule() doesn't re-arm the timer inside the loop. */
    {
        extern void static_htimer_deferred(void);
        static_htimer_deferred();
    }
}

#endif
static void maintenance_interrupt(int irq, void *dev_id)
{
    /*
     * This is a dummy interrupt handler.
     * Receiving the interrupt is going to cause gic_inject to be called
     * on return to guest that is going to clear the old LRs and inject
     * new interrupts.
     *
     * Do not add code here: maintenance interrupts caused by setting
     * GICH_HCR_UIE, might read as spurious interrupts (1023) because
     * GICH_HCR_UIE is cleared before reading GICC_IAR. As a consequence
     * this handler is not called.
     */
    perfc_incr(maintenance_irqs);
}

void gic_dump_info(struct vcpu *v)
{
    printk("GICH_LRs (vcpu %d) mask=%"PRIx64"\n", v->vcpu_id, v->arch.lr_mask);
    gic_hw_ops->dump_state(v);
}

void init_maintenance_interrupt(void)
{
    request_irq(gic_hw_ops->info->maintenance_irq, 0, maintenance_interrupt,
                "irq-maintenance", NULL);
}

void init_maintenance_interrupt_prtos(void) {
    request_irq_prtos(gic_hw_ops->info->maintenance_irq, 0, maintenance_interrupt, "irq-maintenance", NULL);
}

int gic_make_hwdom_dt_node(const struct domain *d,
                           const struct dt_device_node *gic,
                           void *fdt)
{
    ASSERT(gic == dt_interrupt_controller);

    return gic_hw_ops->make_hwdom_dt_node(d, gic, fdt);
}

#ifdef CONFIG_ACPI
int gic_make_hwdom_madt(const struct domain *d, u32 offset)
{
    return gic_hw_ops->make_hwdom_madt(d, offset);
}

unsigned long gic_get_hwdom_madt_size(const struct domain *d)
{
    unsigned long madt_size;

    madt_size = sizeof(struct acpi_table_madt)
                + ACPI_MADT_GICC_LENGTH * d->max_vcpus
                + sizeof(struct acpi_madt_generic_distributor)
                + gic_hw_ops->get_hwdom_extra_madt_size(d);

    return madt_size;
}
#endif


static int cpu_gic_callback(struct notifier_block *nfb,
                            unsigned long action,
                            void *hcpu)
{
    switch ( action )
    {
    case CPU_DYING:
        /* This is reverting the work done in init_maintenance_interrupt */
        release_irq(gic_hw_ops->info->maintenance_irq, NULL);
        break;
    default:
        break;
    }

    return NOTIFY_DONE;
}

static struct notifier_block cpu_gic_nfb = {
    .notifier_call = cpu_gic_callback,
};

static int __init cpu_gic_notifier_init(void)
{
    register_cpu_notifier(&cpu_gic_nfb);

    return 0;
}
__initcall(cpu_gic_notifier_init);

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: gic.c === */
/* === BEGIN INLINED: gic-v3.c === */
#include <xen_xen_config.h>
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/gic-v3.c
 *
 * ARM Generic Interrupt Controller support v3 version
 * based on xen/arch/arm/gic-v2.c and kernel GICv3 driver
 *
 * Copyright (C) 2012,2013 - ARM Ltd
 * Marc Zyngier <marc.zyngier@arm.com>
 *
 * Vijaya Kumar K <vijaya.kumar@caviumnetworks.com>, Cavium Inc
 * ported to Xen
 */
#include <xen_xen_config.h>

#include <xen_acpi.h>
#include <xen_delay.h>
#include <xen_device_tree.h>
#include <xen_errno.h>
#include <xen_init.h>
#include <xen_iocap.h>
#include <xen_irq.h>
#include <xen_lib.h>
#include <xen_libfdt_libfdt.h>
#include <xen_mm.h>
#include <xen_sched.h>
#include <xen_sizes.h>

#include <asm_cpufeature.h>
#include <asm_generic_device.h>
#include <asm_gic.h>
#include <asm_gic_v3_defs.h>
#include <asm_gic_v3_its.h>
#include <asm_io.h>
#include <asm_sysregs.h>

/* Global state */
static struct {
    void __iomem *map_dbase;  /* Mapped address of distributor registers */
    struct rdist_region *rdist_regions;
    uint32_t  rdist_stride;
    unsigned int rdist_count; /* Number of rdist regions count */
    unsigned int nr_priorities;
    spinlock_t lock;
} gicv3;

static struct gic_info gicv3_info;

/* per-cpu re-distributor base */
static DEFINE_PER_CPU(void __iomem*, rbase);

#define GICD                   (gicv3.map_dbase)
#define GICD_RDIST_BASE        (this_cpu(rbase))
#define GICD_RDIST_SGI_BASE    (GICD_RDIST_BASE + SZ_64K)

/*
 * Saves all 16(Max) LR registers. Though number of LRs implemented
 * is implementation specific.
 */
static inline void gicv3_save_lrs(struct vcpu *v)
{
    /* Fall through for all the cases */
    switch ( gicv3_info.nr_lrs )
    {
    case 16:
        v->arch.gic.v3.lr[15] = READ_SYSREG_LR(15);
        fallthrough;
    case 15:
        v->arch.gic.v3.lr[14] = READ_SYSREG_LR(14);
        fallthrough;
    case 14:
        v->arch.gic.v3.lr[13] = READ_SYSREG_LR(13);
        fallthrough;
    case 13:
        v->arch.gic.v3.lr[12] = READ_SYSREG_LR(12);
        fallthrough;
    case 12:
        v->arch.gic.v3.lr[11] = READ_SYSREG_LR(11);
        fallthrough;
    case 11:
        v->arch.gic.v3.lr[10] = READ_SYSREG_LR(10);
        fallthrough;
    case 10:
        v->arch.gic.v3.lr[9] = READ_SYSREG_LR(9);
        fallthrough;
    case 9:
        v->arch.gic.v3.lr[8] = READ_SYSREG_LR(8);
        fallthrough;
    case 8:
        v->arch.gic.v3.lr[7] = READ_SYSREG_LR(7);
        fallthrough;
    case 7:
        v->arch.gic.v3.lr[6] = READ_SYSREG_LR(6);
        fallthrough;
    case 6:
        v->arch.gic.v3.lr[5] = READ_SYSREG_LR(5);
        fallthrough;
    case 5:
        v->arch.gic.v3.lr[4] = READ_SYSREG_LR(4);
        fallthrough;
    case 4:
        v->arch.gic.v3.lr[3] = READ_SYSREG_LR(3);
        fallthrough;
    case 3:
        v->arch.gic.v3.lr[2] = READ_SYSREG_LR(2);
        fallthrough;
    case 2:
        v->arch.gic.v3.lr[1] = READ_SYSREG_LR(1);
        fallthrough;
    case 1:
         v->arch.gic.v3.lr[0] = READ_SYSREG_LR(0);
         break;
    default:
         BUG();
    }
}

/*
 * Restores all 16(Max) LR registers. Though number of LRs implemented
 * is implementation specific.
 */
static inline void gicv3_restore_lrs(const struct vcpu *v)
{
    /* Fall through for all the cases */
    switch ( gicv3_info.nr_lrs )
    {
    case 16:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[15], 15);
        fallthrough;
    case 15:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[14], 14);
        fallthrough;
    case 14:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[13], 13);
        fallthrough;
    case 13:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[12], 12);
        fallthrough;
    case 12:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[11], 11);
        fallthrough;
    case 11:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[10], 10);
        fallthrough;
    case 10:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[9], 9);
        fallthrough;
    case 9:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[8], 8);
        fallthrough;
    case 8:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[7], 7);
        fallthrough;
    case 7:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[6], 6);
        fallthrough;
    case 6:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[5], 5);
        fallthrough;
    case 5:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[4], 4);
        fallthrough;
    case 4:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[3], 3);
        fallthrough;
    case 3:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[2], 2);
        fallthrough;
    case 2:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[1], 1);
        fallthrough;
    case 1:
        WRITE_SYSREG_LR(v->arch.gic.v3.lr[0], 0);
        break;
    default:
         BUG();
    }
}

static uint64_t gicv3_ich_read_lr(int lr)
{
    switch ( lr )
    {
    case 0: return READ_SYSREG_LR(0);
    case 1: return READ_SYSREG_LR(1);
    case 2: return READ_SYSREG_LR(2);
    case 3: return READ_SYSREG_LR(3);
    case 4: return READ_SYSREG_LR(4);
    case 5: return READ_SYSREG_LR(5);
    case 6: return READ_SYSREG_LR(6);
    case 7: return READ_SYSREG_LR(7);
    case 8: return READ_SYSREG_LR(8);
    case 9: return READ_SYSREG_LR(9);
    case 10: return READ_SYSREG_LR(10);
    case 11: return READ_SYSREG_LR(11);
    case 12: return READ_SYSREG_LR(12);
    case 13: return READ_SYSREG_LR(13);
    case 14: return READ_SYSREG_LR(14);
    case 15: return READ_SYSREG_LR(15);
    default:
        BUG();
    }
}

static void gicv3_ich_write_lr(int lr, uint64_t val)
{
    switch ( lr )
    {
    case 0:
        WRITE_SYSREG_LR(val, 0);
        break;
    case 1:
        WRITE_SYSREG_LR(val, 1);
        break;
    case 2:
        WRITE_SYSREG_LR(val, 2);
        break;
    case 3:
        WRITE_SYSREG_LR(val, 3);
        break;
    case 4:
        WRITE_SYSREG_LR(val, 4);
        break;
    case 5:
        WRITE_SYSREG_LR(val, 5);
        break;
    case 6:
        WRITE_SYSREG_LR(val, 6);
        break;
    case 7:
        WRITE_SYSREG_LR(val, 7);
        break;
    case 8:
        WRITE_SYSREG_LR(val, 8);
        break;
    case 9:
        WRITE_SYSREG_LR(val, 9);
        break;
    case 10:
        WRITE_SYSREG_LR(val, 10);
        break;
    case 11:
        WRITE_SYSREG_LR(val, 11);
        break;
    case 12:
        WRITE_SYSREG_LR(val, 12);
        break;
    case 13:
        WRITE_SYSREG_LR(val, 13);
        break;
    case 14:
        WRITE_SYSREG_LR(val, 14);
        break;
    case 15:
        WRITE_SYSREG_LR(val, 15);
        break;
    default:
        return;
    }
    isb();
}

/*
 * System Register Enable (SRE). Enable to access CPU & Virtual
 * interface registers as system registers in EL2
 */
static void gicv3_enable_sre(void)
{
    register_t val;

    val = READ_SYSREG(ICC_SRE_EL2);
    val |= GICC_SRE_EL2_SRE;

    WRITE_SYSREG(val, ICC_SRE_EL2);
    isb();
}

/* Wait for completion of a distributor change */
static void gicv3_do_wait_for_rwp(void __iomem *base)
{
    uint32_t val;
    bool timeout = false;
    s_time_t deadline = NOW() + MILLISECS(1000);

    do {
        val = readl_relaxed(base + GICD_CTLR);
        if ( !(val & GICD_CTLR_RWP) )
            break;
        if ( NOW() > deadline )
        {
            timeout = true;
            break;
        }
        cpu_relax();
        udelay(1);
    } while ( 1 );

    if ( timeout )
        dprintk(XENLOG_ERR, "RWP timeout\n");
}

static void gicv3_dist_wait_for_rwp(void)
{
    gicv3_do_wait_for_rwp(GICD);
}

static void gicv3_redist_wait_for_rwp(void)
{
    gicv3_do_wait_for_rwp(GICD_RDIST_BASE);
}

static void gicv3_wait_for_rwp(int irq)
{
    if ( irq < NR_LOCAL_IRQS )
         gicv3_redist_wait_for_rwp();
    else
         gicv3_dist_wait_for_rwp();
}

static unsigned int gicv3_get_cpu_from_mask(const cpumask_t *cpumask)
{
    unsigned int cpu;
    cpumask_t possible_mask;

    cpumask_and(&possible_mask, cpumask, &cpu_possible_map);
    cpu = cpumask_any(&possible_mask);

    return cpu;
}

static void restore_aprn_regs(const union gic_state_data *d)
{
    /* Write APRn register based on number of priorities
       platform has implemented */
    switch ( gicv3.nr_priorities )
    {
    case 7:
        WRITE_SYSREG(d->v3.apr0[2], ICH_AP0R2_EL2);
        WRITE_SYSREG(d->v3.apr1[2], ICH_AP1R2_EL2);
        /* Fall through */
    case 6:
        WRITE_SYSREG(d->v3.apr0[1], ICH_AP0R1_EL2);
        WRITE_SYSREG(d->v3.apr1[1], ICH_AP1R1_EL2);
        /* Fall through */
    case 5:
        WRITE_SYSREG(d->v3.apr0[0], ICH_AP0R0_EL2);
        WRITE_SYSREG(d->v3.apr1[0], ICH_AP1R0_EL2);
        break;
    default:
        BUG();
    }
}

static void save_aprn_regs(union gic_state_data *d)
{
    /* Read APRn register based on number of priorities
       platform has implemented */
    switch ( gicv3.nr_priorities )
    {
    case 7:
        d->v3.apr0[2] = READ_SYSREG(ICH_AP0R2_EL2);
        d->v3.apr1[2] = READ_SYSREG(ICH_AP1R2_EL2);
        /* Fall through */
    case 6:
        d->v3.apr0[1] = READ_SYSREG(ICH_AP0R1_EL2);
        d->v3.apr1[1] = READ_SYSREG(ICH_AP1R1_EL2);
        /* Fall through */
    case 5:
        d->v3.apr0[0] = READ_SYSREG(ICH_AP0R0_EL2);
        d->v3.apr1[0] = READ_SYSREG(ICH_AP1R0_EL2);
        break;
    default:
        BUG();
    }
}

/*
 * As per section 4.8.17 of the GICv3 spec following
 * registers are save and restored on guest swap
 */
static void gicv3_save_state(struct vcpu *v)
{

    /* No need for spinlocks here because interrupts are disabled around
     * this call and it only accesses struct vcpu fields that cannot be
     * accessed simultaneously by another pCPU.
     *
     * Make sure all stores to the GIC via the memory mapped interface
     * are now visible to the system register interface
     */
    dsb(sy);
    gicv3_save_lrs(v);
    save_aprn_regs(&v->arch.gic);
    v->arch.gic.v3.vmcr = READ_SYSREG(ICH_VMCR_EL2);
    v->arch.gic.v3.sre_el1 = READ_SYSREG(ICC_SRE_EL1);
}

static void gicv3_restore_state(const struct vcpu *v)
{
    register_t val;

    val = READ_SYSREG(ICC_SRE_EL2);
    /*
     * Don't give access to system registers when the guest is using
     * GICv2
     */
    if ( v->domain->arch.vgic.version == GIC_V2 )
        val &= ~GICC_SRE_EL2_ENEL1;
    else
        val |= GICC_SRE_EL2_ENEL1;
    WRITE_SYSREG(val, ICC_SRE_EL2);

    /*
     * VFIQEn is RES1 if ICC_SRE_EL1.SRE is 1. This causes a Group0
     * interrupt (as generated in GICv2 mode) to be delivered as a FIQ
     * to the guest, with potentially consequence. So we must make sure
     * that ICC_SRE_EL1 has been actually programmed with the value we
     * want before starting to mess with the rest of the GIC, and
     * VMCR_EL1 in particular.
     */
    WRITE_SYSREG(v->arch.gic.v3.sre_el1, ICC_SRE_EL1);
    isb();
    WRITE_SYSREG(v->arch.gic.v3.vmcr, ICH_VMCR_EL2);
    restore_aprn_regs(&v->arch.gic);
    gicv3_restore_lrs(v);

    /*
     * Make sure all stores are visible the GIC
     */
    dsb(sy);
}

static void gicv3_dump_state(const struct vcpu *v)
{
    int i;

    if ( v == current )
    {
        for ( i = 0; i < gicv3_info.nr_lrs; i++ )
            printk("   HW_LR[%d]=%" PRIx64 "\n", i, gicv3_ich_read_lr(i));
    }
    else
    {
        for ( i = 0; i < gicv3_info.nr_lrs; i++ )
            printk("   VCPU_LR[%d]=%" PRIx64 "\n", i, v->arch.gic.v3.lr[i]);
    }
}

static void gicv3_poke_irq(struct irq_desc *irqd, u32 offset, bool wait_for_rwp)
{
    u32 mask = 1U << (irqd->irq % 32);
    void __iomem *base;

    if ( irqd->irq < NR_GIC_LOCAL_IRQS )
        base = GICD_RDIST_SGI_BASE;
    else
        base = GICD;

    writel_relaxed(mask, base + offset + (irqd->irq / 32) * 4);

    if ( wait_for_rwp )
        gicv3_wait_for_rwp(irqd->irq);
}

static bool gicv3_peek_irq(struct irq_desc *irqd, u32 offset)
{
    void __iomem *base;
    unsigned int irq = irqd->irq;

    if ( irq >= NR_GIC_LOCAL_IRQS)
        base = GICD + (irq / 32) * 4;
    else
        base = GICD_RDIST_SGI_BASE;

    return !!(readl(base + offset) & (1U << (irq % 32)));
}

static void gicv3_unmask_irq(struct irq_desc *irqd)
{
    gicv3_poke_irq(irqd, GICD_ISENABLER, false);
}

static void gicv3_mask_irq(struct irq_desc *irqd)
{
    gicv3_poke_irq(irqd, GICD_ICENABLER, true);
}

static void gicv3_eoi_irq(struct irq_desc *irqd)
{
    /* Lower the priority */
    WRITE_SYSREG(irqd->irq, ICC_EOIR1_EL1);
    isb();
}

// #if CONFIG_STATIC_IRQ_ROUTING
void prtos_gicv3_eoi_irq(int irq) {
    /* Lower the priority */
    WRITE_SYSREG(irq, ICC_EOIR1_EL1);
    isb();
}




static void prtos_gicv3_dir_irq(int irq)
{
    /* Deactivate */
    WRITE_SYSREG(irq, ICC_DIR_EL1);
    isb();
}

void prtos_gicv3_host_irq_end(int irq)
{
    /* Lower the priority */
    prtos_gicv3_eoi_irq(irq);
    /* Deactivate */
    prtos_gicv3_dir_irq(irq);
}

void prtos_gicv3_enable_spi(int irq)
{
    if (irq < 32 || irq >= 1020)
        return;
    writel_relaxed(1U << (irq % 32), GICD + GICD_ISENABLER + (irq / 32) * 4);
    gicv3_dist_wait_for_rwp();
}

void prtos_gicv3_mask_spi(int irq)
{
    if (irq < 32 || irq >= 1020)
        return;
    writel_relaxed(1U << (irq % 32), GICD + GICD_ICENABLER + (irq / 32) * 4);
    gicv3_dist_wait_for_rwp();
}


// #endif // CONFIG_STATIC_IRQ_ROUTING

static void gicv3_dir_irq(struct irq_desc *irqd)
{
    /* Deactivate */
    WRITE_SYSREG(irqd->irq, ICC_DIR_EL1);
    isb();
}

static unsigned int gicv3_read_irq(void)
{
    register_t irq = READ_SYSREG(ICC_IAR1_EL1);

    dsb(sy);

    /* IRQs are encoded using 23bit. */
    return (irq & GICC_IAR_INTID_MASK);
}

/*
 * This is forcing the active state of an interrupt, somewhat circumventing
 * the normal interrupt flow and the GIC state machine. So use with care
 * and only if you know what you are doing. For this reason we also have to
 * tinker with the _IRQ_INPROGRESS bit here, since the normal IRQ handler
 * will not be involved.
 */
static void gicv3_set_active_state(struct irq_desc *irqd, bool active)
{
    ASSERT(spin_is_locked(&irqd->lock));

    if ( active )
    {
        set_bit(_IRQ_INPROGRESS, &irqd->status);
        gicv3_poke_irq(irqd, GICD_ISACTIVER, false);
    }
    else
    {
        clear_bit(_IRQ_INPROGRESS, &irqd->status);
        gicv3_poke_irq(irqd, GICD_ICACTIVER, false);
    }
}

static void gicv3_set_pending_state(struct irq_desc *irqd, bool pending)
{
    ASSERT(spin_is_locked(&irqd->lock));

    if ( pending )
        /* The _IRQ_INPROGRESS bit will be set when the interrupt fires. */
        gicv3_poke_irq(irqd, GICD_ISPENDR, false);
    else
        /* The _IRQ_INPROGRESS bit will remain unchanged. */
        gicv3_poke_irq(irqd, GICD_ICPENDR, false);
}

static inline uint64_t gicv3_mpidr_to_affinity(int cpu)
{
     uint64_t mpidr = cpu_logical_map(cpu);
     return (
#ifdef CONFIG_ARM_64
             MPIDR_AFFINITY_LEVEL(mpidr, 3) << 32 |
#endif
             MPIDR_AFFINITY_LEVEL(mpidr, 2) << 16 |
             MPIDR_AFFINITY_LEVEL(mpidr, 1) << 8  |
             MPIDR_AFFINITY_LEVEL(mpidr, 0));
}

static void gicv3_set_irq_type(struct irq_desc *desc, unsigned int type)
{
    uint32_t cfg, actual, edgebit;
    void __iomem *base;
    unsigned int irq = desc->irq;

    /* SGI's are always edge-triggered not need to call GICD_ICFGR0 */
    ASSERT(irq >= NR_GIC_SGI);

    spin_lock(&gicv3.lock);

    if ( irq >= NR_GIC_LOCAL_IRQS)
        base = GICD + GICD_ICFGR + (irq / 16) * 4;
    else
        base = GICD_RDIST_SGI_BASE + GICR_ICFGR1;

    cfg = readl_relaxed(base);

    edgebit = 2u << (2 * (irq % 16));
    if ( type & IRQ_TYPE_LEVEL_MASK )
        cfg &= ~edgebit;
    else if ( type & IRQ_TYPE_EDGE_BOTH )
        cfg |= edgebit;

    writel_relaxed(cfg, base);

    actual = readl_relaxed(base);
    if ( ( cfg & edgebit ) ^ ( actual & edgebit ) )
    {
        printk(XENLOG_WARNING "GICv3: WARNING: "
               "CPU%d: Failed to configure IRQ%u as %s-triggered. "
               "H/w forces to %s-triggered.\n",
               smp_processor_id(), desc->irq,
               cfg & edgebit ? "Edge" : "Level",
               actual & edgebit ? "Edge" : "Level");
        desc->arch.type = actual & edgebit ?
            IRQ_TYPE_EDGE_RISING :
            IRQ_TYPE_LEVEL_HIGH;
    }
    spin_unlock(&gicv3.lock);
}

static void gicv3_set_irq_priority(struct irq_desc *desc,
                                   unsigned int priority)
{
    unsigned int irq = desc->irq;

    spin_lock(&gicv3.lock);

    /* Set priority */
    if ( irq < NR_GIC_LOCAL_IRQS )
        writeb_relaxed(priority, GICD_RDIST_SGI_BASE + GICR_IPRIORITYR0 + irq);
    else
        writeb_relaxed(priority, GICD + GICD_IPRIORITYR + irq);

    spin_unlock(&gicv3.lock);
}

static void __init gicv3_dist_init(void)
{
    uint32_t type;
    uint64_t affinity;
    unsigned int nr_lines;
    int i;

    /* Disable the distributor */
    writel_relaxed(0, GICD + GICD_CTLR);

    type = readl_relaxed(GICD + GICD_TYPER);
    nr_lines = 32 * ((type & GICD_TYPE_LINES) + 1);

    if ( type & GICD_TYPE_LPIS )
        gicv3_lpi_init_host_lpis(GICD_TYPE_ID_BITS(type));

    /* Only 1020 interrupts are supported */
    nr_lines = min(1020U, nr_lines);
    gicv3_info.nr_lines = nr_lines;

    printk("GICv3: %d lines, (IID %8.8x).\n",
           nr_lines, readl_relaxed(GICD + GICD_IIDR));

    /* Default all global IRQs to level, active low */
    for ( i = NR_GIC_LOCAL_IRQS; i < nr_lines; i += 16 )
        writel_relaxed(0, GICD + GICD_ICFGR + (i / 16) * 4);

    /* Default priority for global interrupts */
    for ( i = NR_GIC_LOCAL_IRQS; i < nr_lines; i += 4 )
        writel_relaxed(GIC_PRI_IRQ_ALL, GICD + GICD_IPRIORITYR + (i / 4) * 4);

    /* Disable/deactivate all global interrupts */
    for ( i = NR_GIC_LOCAL_IRQS; i < nr_lines; i += 32 )
    {
        writel_relaxed(0xffffffffU, GICD + GICD_ICENABLER + (i / 32) * 4);
        writel_relaxed(0xffffffffU, GICD + GICD_ICACTIVER + (i / 32) * 4);
    }

    /*
     * Configure SPIs as non-secure Group-1. This will only matter
     * if the GIC only has a single security state.
     */
    for ( i = NR_GIC_LOCAL_IRQS; i < nr_lines; i += 32 )
        writel_relaxed(GENMASK(31, 0), GICD + GICD_IGROUPR + (i / 32) * 4);

    gicv3_dist_wait_for_rwp();

    /* Turn on the distributor */
    writel_relaxed(GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1A |
                   GICD_CTLR_ENABLE_G1, GICD + GICD_CTLR);

    /* Route all global IRQs to this CPU */
    affinity = gicv3_mpidr_to_affinity(smp_processor_id());
    /* Make sure we don't broadcast the interrupt */
    affinity &= ~GICD_IROUTER_SPI_MODE_ANY;

    for ( i = NR_GIC_LOCAL_IRQS; i < nr_lines; i++ )
        writeq_relaxed_non_atomic(affinity, GICD + GICD_IROUTER + i * 8);
}

static int gicv3_enable_redist(void)
{
    uint32_t val;
    bool timeout = false;
    s_time_t deadline = NOW() + MILLISECS(1000);

    /* Wake up this CPU redistributor */
    val = readl_relaxed(GICD_RDIST_BASE + GICR_WAKER);
    val &= ~GICR_WAKER_ProcessorSleep;
    writel_relaxed(val, GICD_RDIST_BASE + GICR_WAKER);

    do {
        val = readl_relaxed(GICD_RDIST_BASE + GICR_WAKER);
        if ( !(val & GICR_WAKER_ChildrenAsleep) )
            break;
        if ( NOW() > deadline )
        {
            timeout = true;
            break;
        }
        cpu_relax();
        udelay(1);
    } while ( timeout );

    if ( timeout )
    {
        dprintk(XENLOG_ERR, "GICv3: Redist enable RWP timeout\n");
        return 1;
    }

    return 0;
}

/* Enable LPIs on this redistributor (only useful when the host has an ITS). */
static bool gicv3_enable_lpis(void)
{
    uint32_t val;

    val = readl_relaxed(GICD_RDIST_BASE + GICR_TYPER);
    if ( !(val & GICR_TYPER_PLPIS) )
        return false;

    val = readl_relaxed(GICD_RDIST_BASE + GICR_CTLR);
    writel_relaxed(val | GICR_CTLR_ENABLE_LPIS, GICD_RDIST_BASE + GICR_CTLR);

    /* Make sure the GIC has seen the above */
    wmb();

    return true;
}

static int __init gicv3_populate_rdist(void)
{
    int i;
    uint32_t aff;
    uint32_t reg;
    uint64_t typer;
    uint64_t mpidr = cpu_logical_map(smp_processor_id());

    /*
     * If we ever get a cluster of more than 16 CPUs, just scream.
     */
    if ( (mpidr & 0xff) >= 16 )
          dprintk(XENLOG_WARNING, "GICv3:Cluster with more than 16's cpus\n");

    /*
     * Convert affinity to a 32bit value that can be matched to GICR_TYPER
     * bits [63:32]
     */
    aff = (
#ifdef CONFIG_ARM_64
           MPIDR_AFFINITY_LEVEL(mpidr, 3) << 24 |
#endif
           MPIDR_AFFINITY_LEVEL(mpidr, 2) << 16 |
           MPIDR_AFFINITY_LEVEL(mpidr, 1) << 8 |
           MPIDR_AFFINITY_LEVEL(mpidr, 0));

    for ( i = 0; i < gicv3.rdist_count; i++ )
    {
        void __iomem *ptr = gicv3.rdist_regions[i].map_base;

        reg = readl_relaxed(ptr + GICR_PIDR2) & GIC_PIDR2_ARCH_MASK;
        if ( reg != GIC_PIDR2_ARCH_GICv3 && reg != GIC_PIDR2_ARCH_GICv4 )
        {
            dprintk(XENLOG_ERR,
                    "GICv3: No redistributor present @%"PRIpaddr"\n",
                    gicv3.rdist_regions[i].base);
            break;
        }

        do {
            typer = readq_relaxed_non_atomic(ptr + GICR_TYPER);

            if ( (typer >> 32) == aff )
            {
                this_cpu(rbase) = ptr;

                if ( typer & GICR_TYPER_PLPIS )
                {
                    paddr_t rdist_addr;
                    unsigned int procnum;
                    int ret;

                    /*
                     * The ITS refers to redistributors either by their physical
                     * address or by their ID. Which one to use is an ITS
                     * choice. So determine those two values here (which we
                     * can do only here in GICv3 code) and tell the
                     * ITS code about it, so it can use them later to be able
                     * to address those redistributors accordingly.
                     */
                    rdist_addr = gicv3.rdist_regions[i].base;
                    rdist_addr += ptr - gicv3.rdist_regions[i].map_base;
                    procnum = (typer & GICR_TYPER_PROC_NUM_MASK);
                    procnum >>= GICR_TYPER_PROC_NUM_SHIFT;

                    gicv3_set_redist_address(rdist_addr, procnum);

                    ret = gicv3_lpi_init_rdist(ptr);
                    if ( ret && ret != -ENODEV )
                    {
                        printk("GICv3: CPU%d: Cannot initialize LPIs: %u\n",
                               smp_processor_id(), ret);
                        break;
                    }
                }

                printk("GICv3: CPU%d: Found redistributor in region %d @%p\n",
                        smp_processor_id(), i, ptr);
                return 0;
            }

            if ( gicv3.rdist_regions[i].single_rdist )
                break;

            if ( gicv3.rdist_stride )
                ptr += gicv3.rdist_stride;
            else
            {
                ptr += SZ_64K * 2; /* Skip RD_base + SGI_base */
                if ( typer & GICR_TYPER_VLPIS )
                    ptr += SZ_64K * 2; /* Skip VLPI_base + reserved page */
            }

        } while ( !(typer & GICR_TYPER_LAST) );
    }

    dprintk(XENLOG_ERR, "GICv3: CPU%d: mpidr 0x%"PRIregister" has no re-distributor!\n",
            smp_processor_id(), cpu_logical_map(smp_processor_id()));

    return -ENODEV;
}

static int gicv3_cpu_init(void)
{
    int i, ret;

    /* Register ourselves with the rest of the world */
    if ( gicv3_populate_rdist() )
        return -ENODEV;

    if ( gicv3_enable_redist() )
        return -ENODEV;

    /* If the host has any ITSes, enable LPIs now. */
    if ( gicv3_its_host_has_its() )
    {
        if ( !gicv3_enable_lpis() )
            return -EBUSY;
        ret = gicv3_its_setup_collection(smp_processor_id());
        if ( ret )
            return ret;
    }

    /* Set priority on PPI and SGI interrupts */
    for (i = 0; i < NR_GIC_SGI; i += 4)
        writel_relaxed(GIC_PRI_IPI_ALL,
                GICD_RDIST_SGI_BASE + GICR_IPRIORITYR0 + (i / 4) * 4);

    for (i = NR_GIC_SGI; i < NR_GIC_LOCAL_IRQS; i += 4)
        writel_relaxed(GIC_PRI_IRQ_ALL,
                GICD_RDIST_SGI_BASE + GICR_IPRIORITYR0 + (i / 4) * 4);

    /*
     * The activate state is unknown at boot, so make sure all
     * SGIs and PPIs are de-activated.
     */
    writel_relaxed(0xffffffffU, GICD_RDIST_SGI_BASE + GICR_ICACTIVER0);
    /*
     * Disable all PPI interrupts, ensure all SGI interrupts are
     * enabled.
     */
    writel_relaxed(0xffff0000U, GICD_RDIST_SGI_BASE + GICR_ICENABLER0);
    writel_relaxed(0x0000ffffU, GICD_RDIST_SGI_BASE + GICR_ISENABLER0);
    /* Configure SGIs/PPIs as non-secure Group-1 */
    writel_relaxed(GENMASK(31, 0), GICD_RDIST_SGI_BASE + GICR_IGROUPR0);

    gicv3_redist_wait_for_rwp();

    /* Enable system registers */
    gicv3_enable_sre();

    /* No priority grouping */
    WRITE_SYSREG(0, ICC_BPR1_EL1);

    /* Set priority mask register */
    WRITE_SYSREG(DEFAULT_PMR_VALUE, ICC_PMR_EL1);

    /* EOI drops priority, DIR deactivates the interrupt (mode 1) */
    WRITE_SYSREG(GICC_CTLR_EL1_EOImode_drop, ICC_CTLR_EL1);

    /* Enable Group1 interrupts */
    WRITE_SYSREG(1, ICC_IGRPEN1_EL1);

    /* Sync at once at the end of cpu interface configuration */
    isb();

    return 0;
}

static void gicv3_cpu_disable(void)
{
    WRITE_SYSREG(0, ICC_CTLR_EL1);
    isb();
}

static void gicv3_hyp_init(void)
{
    register_t vtr;

    vtr = READ_SYSREG(ICH_VTR_EL2);
    gicv3_info.nr_lrs  = (vtr & ICH_VTR_NRLRGS) + 1;
    gicv3.nr_priorities = ((vtr >> ICH_VTR_PRIBITS_SHIFT) &
                          ICH_VTR_PRIBITS_MASK) + 1;

    if ( !((gicv3.nr_priorities > 4) && (gicv3.nr_priorities < 8)) )
        panic("GICv3: Invalid number of priority bits\n");

    WRITE_SYSREG(ICH_VMCR_EOI | ICH_VMCR_VENG1, ICH_VMCR_EL2);
    WRITE_SYSREG(GICH_HCR_EN, ICH_HCR_EL2);
}

/* Set up the per-CPU parts of the GIC for a secondary CPU */
static int gicv3_secondary_cpu_init(void)
{
    int res;

    spin_lock(&gicv3.lock);

    res = gicv3_cpu_init();
    if ( res )
        goto out;

    gicv3_hyp_init();

out:
    spin_unlock(&gicv3.lock);

    return res;
}

static void gicv3_hyp_disable(void)
{
    register_t hcr;

    hcr = READ_SYSREG(ICH_HCR_EL2);
    hcr &= ~GICH_HCR_EN;
    WRITE_SYSREG(hcr, ICH_HCR_EL2);
    isb();
}

static u16 gicv3_compute_target_list(int *base_cpu, const struct cpumask *mask,
                                     uint64_t cluster_id)
{
    int cpu = *base_cpu;
    uint64_t mpidr = cpu_logical_map(cpu);
    u16 tlist = 0;

    while ( cpu < nr_cpu_ids )
    {
        /*
         * Assume that each cluster does not have more than 16 CPU's.
         * Check is made during GICv3 initialization (gicv3_populate_rdist())
         * on mpidr value for this. So skip this check here.
         */
        tlist |= 1 << (mpidr & 0xf);

        cpu = cpumask_next(cpu, mask);
        if ( cpu == nr_cpu_ids )
        {
            cpu--;
            goto out;
        }

        mpidr = cpu_logical_map(cpu);
        if ( cluster_id != (mpidr & ~MPIDR_AFF0_MASK) ) {
            cpu--;
            goto out;
        }
    }
out:
    *base_cpu = cpu;

    return tlist;
}

static void gicv3_send_sgi_list(enum gic_sgi sgi, const cpumask_t *cpumask)
{
    int cpu = 0;
    uint64_t val;

    for_each_cpu(cpu, cpumask)
    {
        /* Mask lower 8 bits. It represent cpu in affinity level 0 */
        uint64_t cluster_id = cpu_logical_map(cpu) & ~MPIDR_AFF0_MASK;
        u16 tlist;

        /* Get targetlist for the cluster to send SGI */
        tlist = gicv3_compute_target_list(&cpu, cpumask, cluster_id);

        /*
         * Prepare affinity path of the cluster for which SGI is generated
         * along with SGI number
         */
        val = (
#ifdef CONFIG_ARM_64
               MPIDR_AFFINITY_LEVEL(cluster_id, 3) << 48  |
#endif
               MPIDR_AFFINITY_LEVEL(cluster_id, 2) << 32  |
               sgi << 24                                  |
               MPIDR_AFFINITY_LEVEL(cluster_id, 1) << 16  |
               tlist);

        WRITE_SYSREG64(val, ICC_SGI1R_EL1);
    }
    /* Force above writes to ICC_SGI1R_EL1 */
    isb();
}

static void gicv3_send_sgi(enum gic_sgi sgi, enum gic_sgi_mode mode,
                           const cpumask_t *cpumask)
{
    /*
     * Ensure that stores to Normal memory are visible to the other CPUs
     * before issuing the IPI.
     */
    dsb(st);

    switch ( mode )
    {
    case SGI_TARGET_OTHERS:
        WRITE_SYSREG64(ICH_SGI_TARGET_OTHERS << ICH_SGI_IRQMODE_SHIFT |
                       (uint64_t)sgi << ICH_SGI_IRQ_SHIFT,
                       ICC_SGI1R_EL1);
        isb();
        break;
    case SGI_TARGET_SELF:
        gicv3_send_sgi_list(sgi, cpumask_of(smp_processor_id()));
        break;
    case SGI_TARGET_LIST:
        gicv3_send_sgi_list(sgi, cpumask);
        break;
    default:
        BUG();
    }
}

/* Shut down the per-CPU GIC interface */
static void gicv3_disable_interface(void)
{
    spin_lock(&gicv3.lock);

    gicv3_cpu_disable();
    gicv3_hyp_disable();

    spin_unlock(&gicv3.lock);
}

static void gicv3_update_lr(int lr, unsigned int virq, uint8_t priority,
                            unsigned int hw_irq, unsigned int state)
{
    uint64_t val = 0;

    BUG_ON(lr >= gicv3_info.nr_lrs);
    BUG_ON(lr < 0);

    val =  (((uint64_t)state & 0x3) << ICH_LR_STATE_SHIFT);

    /*
     * When the guest is GICv3, all guest IRQs are Group 1, as Group0
     * would result in a FIQ in the guest, which it wouldn't expect
     */
    if ( current->domain->arch.vgic.version == GIC_V3 )
        val |= ICH_LR_GRP1;

    val |= (uint64_t)priority << ICH_LR_PRIORITY_SHIFT;
    val |= ((uint64_t)virq & ICH_LR_VIRTUAL_MASK) << ICH_LR_VIRTUAL_SHIFT;

   if ( hw_irq != INVALID_IRQ )
       val |= ICH_LR_HW | (((uint64_t)hw_irq & ICH_LR_PHYSICAL_MASK)
                           << ICH_LR_PHYSICAL_SHIFT);

    gicv3_ich_write_lr(lr, val);
}

static void gicv3_clear_lr(int lr)
{
    gicv3_ich_write_lr(lr, 0);
}

static void gicv3_read_lr(int lr, struct gic_lr *lr_reg)
{
    uint64_t lrv;

    lrv = gicv3_ich_read_lr(lr);

    lr_reg->virq = (lrv >> ICH_LR_VIRTUAL_SHIFT) & ICH_LR_VIRTUAL_MASK;

    lr_reg->priority  = (lrv >> ICH_LR_PRIORITY_SHIFT) & ICH_LR_PRIORITY_MASK;
    lr_reg->pending   = lrv & ICH_LR_STATE_PENDING;
    lr_reg->active    = lrv & ICH_LR_STATE_ACTIVE;
    lr_reg->hw_status = lrv & ICH_LR_HW;

    if ( lr_reg->hw_status )
        lr_reg->hw.pirq = (lrv >> ICH_LR_PHYSICAL_SHIFT) & ICH_LR_PHYSICAL_MASK;
    else
    {
        lr_reg->virt.eoi = (lrv & ICH_LR_MAINTENANCE_IRQ);
        /* Source only exists in GICv2 compatible mode */
        if ( current->domain->arch.vgic.version == GIC_V2 )
        {
            /*
             * This is only valid for SGI, but it does not matter to always
             * read it as it should be 0 by default.
             */
            lr_reg->virt.source = (lrv >> ICH_LR_CPUID_SHIFT)
                & ICH_LR_CPUID_MASK;
        }
    }
}

static void gicv3_write_lr(int lr, const struct gic_lr *lr_reg)
{
    uint64_t lrv = 0;
    const enum gic_version vgic_version = current->domain->arch.vgic.version;


    lrv = ( ((u64)(lr_reg->virq & ICH_LR_VIRTUAL_MASK)  << ICH_LR_VIRTUAL_SHIFT) |
        ((u64)(lr_reg->priority & ICH_LR_PRIORITY_MASK) << ICH_LR_PRIORITY_SHIFT) );

    if ( lr_reg->active )
        lrv |= ICH_LR_STATE_ACTIVE;

    if ( lr_reg->pending )
        lrv |= ICH_LR_STATE_PENDING;

    if ( lr_reg->hw_status )
    {
        lrv |= ICH_LR_HW;
        lrv |= (uint64_t)lr_reg->hw.pirq << ICH_LR_PHYSICAL_SHIFT;
    }
    else
    {
        if ( lr_reg->virt.eoi )
            lrv |= ICH_LR_MAINTENANCE_IRQ;
        /* Source is only set in GICv2 compatible mode */
        if ( vgic_version == GIC_V2 )
        {
            /*
             * Source is only valid for SGIs, the caller should make
             * sure the field virt.source is always 0 for non-SGI.
             */
            ASSERT(!lr_reg->virt.source || lr_reg->virq < NR_GIC_SGI);
            lrv |= (uint64_t)lr_reg->virt.source << ICH_LR_CPUID_SHIFT;
        }
    }

    /*
     * When the guest is using vGICv3, all the IRQs are Group 1. Group 0
     * would result in a FIQ, which will not be expected by the guest OS.
     */
    if ( vgic_version == GIC_V3 )
        lrv |= ICH_LR_GRP1;

    gicv3_ich_write_lr(lr, lrv);
}

static void gicv3_hcr_status(uint32_t flag, bool status)
{
    register_t hcr;

    hcr = READ_SYSREG(ICH_HCR_EL2);
    if ( status )
        WRITE_SYSREG(hcr | flag, ICH_HCR_EL2);
    else
        WRITE_SYSREG(hcr & (~flag), ICH_HCR_EL2);
    isb();
}

static unsigned int gicv3_read_vmcr_priority(void)
{
   return ((READ_SYSREG(ICH_VMCR_EL2) >> ICH_VMCR_PRIORITY_SHIFT) &
            ICH_VMCR_PRIORITY_MASK);
}

/* Only support reading GRP1 APRn registers */
static unsigned int gicv3_read_apr(int apr_reg)
{
    register_t apr;

    switch ( apr_reg )
    {
    case 0:
        ASSERT(gicv3.nr_priorities > 4 && gicv3.nr_priorities < 8);
        apr = READ_SYSREG(ICH_AP1R0_EL2);
        break;
    case 1:
        ASSERT(gicv3.nr_priorities > 5 && gicv3.nr_priorities < 8);
        apr = READ_SYSREG(ICH_AP1R1_EL2);
        break;
    case 2:
        ASSERT(gicv3.nr_priorities > 6 && gicv3.nr_priorities < 8);
        apr = READ_SYSREG(ICH_AP1R2_EL2);
        break;
    default:
        BUG();
    }

    /* Number of priority levels do not exceed 32bit. */
    return apr;
}

static bool gicv3_read_pending_state(struct irq_desc *irqd)
{
    return gicv3_peek_irq(irqd, GICD_ISPENDR);
}

static void gicv3_irq_enable(struct irq_desc *desc)
{
    unsigned long flags;

    ASSERT(spin_is_locked(&desc->lock));

    spin_lock_irqsave(&gicv3.lock, flags);
    clear_bit(_IRQ_DISABLED, &desc->status);
    dsb(sy);
    /* Enable routing */
    gicv3_unmask_irq(desc);
    spin_unlock_irqrestore(&gicv3.lock, flags);
}

static void gicv3_irq_disable(struct irq_desc *desc)
{
    unsigned long flags;

    ASSERT(spin_is_locked(&desc->lock));

    spin_lock_irqsave(&gicv3.lock, flags);
    /* Disable routing */
    gicv3_mask_irq(desc);
    set_bit(_IRQ_DISABLED, &desc->status);
    spin_unlock_irqrestore(&gicv3.lock, flags);
}

static unsigned int gicv3_irq_startup(struct irq_desc *desc)
{
    gicv3_irq_enable(desc);

    return 0;
}

static void gicv3_irq_shutdown(struct irq_desc *desc)
{
    gicv3_irq_disable(desc);
}

static void gicv3_irq_ack(struct irq_desc *desc)
{
    /* No ACK -- reading IAR has done this for us */
}

static void gicv3_host_irq_end(struct irq_desc *desc)
{
    /* Lower the priority */
    gicv3_eoi_irq(desc);
    /* Deactivate */
    gicv3_dir_irq(desc);
}

static void gicv3_guest_irq_end(struct irq_desc *desc)
{
    /* Lower the priority of the IRQ */
    gicv3_eoi_irq(desc);
    /* Deactivation happens in maintenance interrupt / via GICV */
}

static void gicv3_irq_set_affinity(struct irq_desc *desc, const cpumask_t *mask)
{
    unsigned int cpu;
    uint64_t affinity;

    ASSERT(!cpumask_empty(mask));

    spin_lock(&gicv3.lock);

    cpu = gicv3_get_cpu_from_mask(mask);
    affinity = gicv3_mpidr_to_affinity(cpu);
    /* Make sure we don't broadcast the interrupt */
    affinity &= ~GICD_IROUTER_SPI_MODE_ANY;

    if ( desc->irq >= NR_GIC_LOCAL_IRQS )
        writeq_relaxed_non_atomic(affinity, (GICD + GICD_IROUTER + desc->irq * 8));

    spin_unlock(&gicv3.lock);
}

static int gicv3_make_hwdom_dt_node(const struct domain *d,
                                    const struct dt_device_node *gic,
                                    void *fdt)
{
    const void *compatible, *hw_reg;
    uint32_t len, new_len;
    int res;

    compatible = dt_get_property(gic, "compatible", &len);
    if ( !compatible )
    {
        dprintk(XENLOG_ERR, "Can't find compatible property for the gic node\n");
        return -FDT_ERR_XEN(ENOENT);
    }

    res = fdt_property(fdt, "compatible", compatible, len);
    if ( res )
        return res;

    res = fdt_property_cell(fdt, "#redistributor-regions",
                            d->arch.vgic.nr_regions);
    if ( res )
        return res;

    new_len = dt_cells_to_size(dt_n_addr_cells(gic) + dt_n_size_cells(gic));
    /*
     * GIC has two memory regions: Distributor + rdist regions
     * CPU interface and virtual cpu interfaces accessesed as System registers
     * So cells are created only for Distributor and rdist regions
     * The hardware domain may not use all the regions. So only copy
     * what is necessary.
     */
    new_len = new_len * (d->arch.vgic.nr_regions + 1);

    hw_reg = dt_get_property(gic, "reg", &len);
    if ( !hw_reg )
        return -FDT_ERR_XEN(ENOENT);
    if ( new_len > len )
        return -FDT_ERR_XEN(ERANGE);

    res = fdt_property(fdt, "reg", hw_reg, new_len);
    if ( res )
        return res;

    return gicv3_its_make_hwdom_dt_nodes(d, gic, fdt);
}

static const hw_irq_controller gicv3_host_irq_type = {
    .typename     = "gic-v3",
    .startup      = gicv3_irq_startup,
    .shutdown     = gicv3_irq_shutdown,
    .enable       = gicv3_irq_enable,
    .disable      = gicv3_irq_disable,
    .ack          = gicv3_irq_ack,
    .end          = gicv3_host_irq_end,
    .set_affinity = gicv3_irq_set_affinity,
};

static const hw_irq_controller gicv3_guest_irq_type = {
    .typename     = "gic-v3",
    .startup      = gicv3_irq_startup,
    .shutdown     = gicv3_irq_shutdown,
    .enable       = gicv3_irq_enable,
    .disable      = gicv3_irq_disable,
    .ack          = gicv3_irq_ack,
    .end          = gicv3_guest_irq_end,
    .set_affinity = gicv3_irq_set_affinity,
};

static paddr_t __initdata dbase = INVALID_PADDR;
static paddr_t __initdata vbase = INVALID_PADDR, vsize = 0;
static paddr_t __initdata cbase = INVALID_PADDR, csize = 0;

#ifdef CONFIG_VGICV2

#else
static inline void gicv3_init_v2(void) { }
#endif

static void __init gicv3_ioremap_distributor(paddr_t dist_paddr)
{
    if ( dist_paddr & ~PAGE_MASK )
        panic("GICv3:  Found unaligned distributor address %"PRIpaddr"\n",
              dbase);

    gicv3.map_dbase = ioremap_nocache(dist_paddr, SZ_64K);
    if ( !gicv3.map_dbase )
        panic("GICv3: Failed to ioremap for GIC distributor\n");
}

static struct rdist_region rdist_region_prtos;

static void __init gicv3_dt_init_prtos(void)
{
    struct rdist_region *rdist_regs;
    int res, i;
    // const struct dt_device_node *node = gicv3_info.node;

    // res = dt_device_get_paddr(node, 0, &dbase, NULL);
    // if ( res )
    //     panic("GICv3: Cannot find a valid distributor address\n");

    dbase = 0x8000000;
    gicv3_ioremap_distributor(dbase);

    // if ( !dt_property_read_u32(node, "#redistributor-regions",
    //             &gicv3.rdist_count) )
        gicv3.rdist_count = 1;

    //rdist_regs = xzalloc_array(struct rdist_region, gicv3.rdist_count);
    
    rdist_regs = &rdist_region_prtos;
    if ( !rdist_regs )
        panic("GICv3: Failed to allocate memory for rdist regions\n");

    // for ( i = 0; i < gicv3.rdist_count; i++ )
    // {
    //     paddr_t rdist_base, rdist_size;

    //     res = dt_device_get_paddr(node, 1 + i, &rdist_base, &rdist_size);
    //     if ( res )
    //         panic("GICv3: No rdist base found for region %d\n", i);

    //     rdist_regs[i].base = rdist_base;
    //     rdist_regs[i].size = rdist_size;
    // }

    rdist_regs[0].base = 0x80A0000;
    rdist_regs[0].size = 0xF60000;

    // if ( !dt_property_read_u32(node, "redistributor-stride", &gicv3.rdist_stride) )
        gicv3.rdist_stride = 0;

    gicv3.rdist_regions= rdist_regs;

    // res = platform_get_irq(node, 0);
    // if ( res < 0 )
    //     panic("GICv3: Cannot find the maintenance IRQ\n");
    gicv3_info.maintenance_irq = 25;  // 25 for maintenance IRQ

    /*
     * For GICv3 supporting GICv2, GICC and GICV base address will be
     * provided.
     */
    // res = dt_device_get_paddr(node, 1 + gicv3.rdist_count,
    //                             &cbase, &csize);
    // if ( !res )
    //     dt_device_get_paddr(node, 1 + gicv3.rdist_count + 2,
    //                           &vbase, &vsize);
}


static void __init gicv3_dt_init(void)
{
    struct rdist_region *rdist_regs;
    int res, i;
    const struct dt_device_node *node = gicv3_info.node;

    res = dt_device_get_paddr(node, 0, &dbase, NULL);
    if ( res )
        panic("GICv3: Cannot find a valid distributor address\n");

    gicv3_ioremap_distributor(dbase);

    if ( !dt_property_read_u32(node, "#redistributor-regions",
                &gicv3.rdist_count) )
        gicv3.rdist_count = 1;

    rdist_regs = xzalloc_array(struct rdist_region, gicv3.rdist_count);
    if ( !rdist_regs )
        panic("GICv3: Failed to allocate memory for rdist regions\n");

    for ( i = 0; i < gicv3.rdist_count; i++ )
    {
        paddr_t rdist_base, rdist_size;

        res = dt_device_get_paddr(node, 1 + i, &rdist_base, &rdist_size);
        if ( res )
            panic("GICv3: No rdist base found for region %d\n", i);

        rdist_regs[i].base = rdist_base;
        rdist_regs[i].size = rdist_size;
    }

    if ( !dt_property_read_u32(node, "redistributor-stride", &gicv3.rdist_stride) )
        gicv3.rdist_stride = 0;

    gicv3.rdist_regions= rdist_regs;

    res = platform_get_irq(node, 0);
    if ( res < 0 )
        panic("GICv3: Cannot find the maintenance IRQ\n");
    gicv3_info.maintenance_irq = res;

    /*
     * For GICv3 supporting GICv2, GICC and GICV base address will be
     * provided.
     */
    res = dt_device_get_paddr(node, 1 + gicv3.rdist_count,
                                &cbase, &csize);
    if ( !res )
        dt_device_get_paddr(node, 1 + gicv3.rdist_count + 2,
                              &vbase, &vsize);
}

static int gicv3_iomem_deny_access(struct domain *d)
{
    int rc, i;
    unsigned long mfn, nr;

    mfn = dbase >> PAGE_SHIFT;
    nr = PFN_UP(SZ_64K);
    rc = iomem_deny_access(d, mfn, mfn + nr);
    if ( rc )
        return rc;

    rc = gicv3_its_deny_access(d);
    if ( rc )
        return rc;

    for ( i = 0; i < gicv3.rdist_count; i++ )
    {
        mfn = gicv3.rdist_regions[i].base >> PAGE_SHIFT;
        nr = PFN_UP(gicv3.rdist_regions[i].size);
        rc = iomem_deny_access(d, mfn, mfn + nr);
        if ( rc )
            return rc;
    }

    if ( cbase != INVALID_PADDR )
    {
        mfn = cbase >> PAGE_SHIFT;
        nr = PFN_UP(csize);
        rc = iomem_deny_access(d, mfn, mfn + nr);
        if ( rc )
            return rc;
    }

    if ( vbase != INVALID_PADDR )
    {
        mfn = vbase >> PAGE_SHIFT;
        nr = PFN_UP(csize);
        return iomem_deny_access(d, mfn, mfn + nr);
    }

    return 0;
}

#ifdef CONFIG_ACPI
static void __init
gic_acpi_add_rdist_region(paddr_t base, paddr_t size, bool single_rdist)
{
    unsigned int idx = gicv3.rdist_count++;

    gicv3.rdist_regions[idx].single_rdist = single_rdist;
    gicv3.rdist_regions[idx].base = base;
    gicv3.rdist_regions[idx].size = size;
}

static inline bool gic_dist_supports_dvis(void)
{
    return !!(readl_relaxed(GICD + GICD_TYPER) & GICD_TYPER_DVIS);
}

static int gicv3_make_hwdom_madt(const struct domain *d, u32 offset)
{
    struct acpi_subtable_header *header;
    struct acpi_madt_generic_interrupt *host_gicc, *gicc;
    struct acpi_madt_generic_redistributor *gicr;
    u8 *base_ptr = d->arch.efi_acpi_table + offset;
    u32 i, table_len = 0, size;

    /* Add Generic Interrupt */
    header = acpi_table_get_entry_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT, 0);
    if ( !header )
    {
        printk("Can't get GICC entry");
        return -EINVAL;
    }

    host_gicc = container_of(header, struct acpi_madt_generic_interrupt,
                             header);
    size = ACPI_MADT_GICC_LENGTH;
    for ( i = 0; i < d->max_vcpus; i++ )
    {
        gicc = (struct acpi_madt_generic_interrupt *)(base_ptr + table_len);
        memcpy(gicc, host_gicc, size);
        gicc->cpu_interface_number = i;
        gicc->uid = i;
        gicc->flags = ACPI_MADT_ENABLED;
        gicc->arm_mpidr = vcpuid_to_vaffinity(i);
        gicc->parking_version = 0;
        gicc->performance_interrupt = 0;
        gicc->gicv_base_address = 0;
        gicc->gich_base_address = 0;
        gicc->gicr_base_address = 0;
        gicc->vgic_interrupt = 0;
        table_len += size;
    }

    /* Add Generic Redistributor */
    size = sizeof(struct acpi_madt_generic_redistributor);
    /*
     * The hardware domain may not used all the regions. So only copy
     * what is necessary.
     */
    for ( i = 0; i < d->arch.vgic.nr_regions; i++ )
    {
        gicr = (struct acpi_madt_generic_redistributor *)(base_ptr + table_len);
        gicr->header.type = ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR;
        gicr->header.length = size;
        gicr->base_address = gicv3.rdist_regions[i].base;
        gicr->length = gicv3.rdist_regions[i].size;
        table_len += size;
    }

    table_len += gicv3_its_make_hwdom_madt(d, base_ptr + table_len);

    return table_len;
}

static unsigned long gicv3_get_hwdom_extra_madt_size(const struct domain *d)
{
    unsigned long size;

    size = sizeof(struct acpi_madt_generic_redistributor) * gicv3.rdist_count;

    size += sizeof(struct acpi_madt_generic_translator)
            * vgic_v3_its_count(d);

    return size;
}

static int __init
gic_acpi_parse_madt_cpu(struct acpi_subtable_header *header,
                        const unsigned long end)
{
    static int cpu_base_assigned = 0;
    struct acpi_madt_generic_interrupt *processor =
               container_of(header, struct acpi_madt_generic_interrupt, header);

    if ( BAD_MADT_GICC_ENTRY(processor, end) )
        return -EINVAL;

    /* Read from APIC table and fill up the GIC variables */
    if ( !cpu_base_assigned )
    {
        cbase = processor->base_address;
        vbase = processor->gicv_base_address;
        gicv3_info.maintenance_irq = processor->vgic_interrupt;

        if ( processor->flags & ACPI_MADT_VGIC_IRQ_MODE )
            irq_set_type(gicv3_info.maintenance_irq, IRQ_TYPE_EDGE_BOTH);
        else
            irq_set_type(gicv3_info.maintenance_irq, IRQ_TYPE_LEVEL_MASK);

        cpu_base_assigned = 1;
    }
    else
    {
        if ( cbase != processor->base_address
             || vbase != processor->gicv_base_address
             || gicv3_info.maintenance_irq != processor->vgic_interrupt )
        {
            printk("GICv3: GICC entries are not same in MADT table\n");
            return -EINVAL;
        }
    }

    return 0;
}

static int __init
gic_acpi_parse_madt_distributor(struct acpi_subtable_header *header,
                                const unsigned long end)
{
    struct acpi_madt_generic_distributor *dist =
             container_of(header, struct acpi_madt_generic_distributor, header);

    if ( BAD_MADT_ENTRY(dist, end) )
        return -EINVAL;

    dbase = dist->base_address;

    return 0;
}

static int __init
gic_acpi_parse_cpu_redistributor(struct acpi_subtable_header *header,
                                 const unsigned long end)
{
    struct acpi_madt_generic_interrupt *processor;
    u32 size;

    processor = (struct acpi_madt_generic_interrupt *)header;
    if ( !(processor->flags & ACPI_MADT_ENABLED) )
        return 0;

    size = gic_dist_supports_dvis() ? 4 * SZ_64K : 2 * SZ_64K;
    gic_acpi_add_rdist_region(processor->gicr_base_address, size, true);

    return 0;
}

static int __init
gic_acpi_get_madt_cpu_num(struct acpi_subtable_header *header,
                          const unsigned long end)
{
    struct acpi_madt_generic_interrupt *cpuif;

    cpuif = (struct acpi_madt_generic_interrupt *)header;
    if ( BAD_MADT_GICC_ENTRY(cpuif, end) || !cpuif->gicr_base_address )
        return -EINVAL;

    return 0;
}

static int __init
gic_acpi_parse_madt_redistributor(struct acpi_subtable_header *header,
                                  const unsigned long end)
{
    struct acpi_madt_generic_redistributor *rdist;

    rdist = (struct acpi_madt_generic_redistributor *)header;
    if ( BAD_MADT_ENTRY(rdist, end) )
        return -EINVAL;

    gic_acpi_add_rdist_region(rdist->base_address, rdist->length, false);

    return 0;
}

static int __init
gic_acpi_get_madt_redistributor_num(struct acpi_subtable_header *header,
                                    const unsigned long end)
{
    /* Nothing to do here since it only wants to get the number of GIC
     * redistributors.
     */
    return 0;
}

static void __init gicv3_acpi_init(void)
{
    struct rdist_region *rdist_regs;
    bool gicr_table = true;
    int count;

    /*
     * Find distributor base address. We expect one distributor entry since
     * ACPI 5.0 spec neither support multi-GIC instances nor GIC cascade.
     */
    count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR,
                                  gic_acpi_parse_madt_distributor, 0);
    if ( count <= 0 )
        panic("GICv3: No valid GICD entries exists\n");

    gicv3_ioremap_distributor(dbase);

    /* Get number of redistributor */
    count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR,
                                  gic_acpi_get_madt_redistributor_num, 0);
    /* Count the total number of CPU interface entries */
    if ( count <= 0 ) {
        count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
                                      gic_acpi_get_madt_cpu_num, 0);
        if (count <= 0)
            panic("GICv3: No valid GICR entries exists\n");

        gicr_table = false;
    }

    rdist_regs = xzalloc_array(struct rdist_region, count);
    if ( !rdist_regs )
        panic("GICv3: Failed to allocate memory for rdist regions\n");

    gicv3.rdist_regions = rdist_regs;

    if ( gicr_table )
        /* Parse always-on power domain Re-distributor entries */
        count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR,
                                      gic_acpi_parse_madt_redistributor, count);
    else
        /* Parse Re-distributor entries described in CPU interface table */
        count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
                                      gic_acpi_parse_cpu_redistributor, count);
    if ( count <= 0 )
        panic("GICv3: Can't get Redistributor entry\n");

    /* Collect CPU base addresses */
    count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
                                  gic_acpi_parse_madt_cpu, 0);
    if ( count <= 0 )
        panic("GICv3: No valid GICC entries exists\n");

    gicv3.rdist_stride = 0;

    /*
     * In ACPI, 0 is considered as the invalid address. However the rest
     * of the initialization rely on the invalid address to be
     * INVALID_ADDR.
     *
     * Also set the size of the GICC and GICV when there base address
     * is not invalid as those values are not present in ACPI.
     */
    if ( !cbase )
        cbase = INVALID_PADDR;
    else
        csize = SZ_8K;

    if ( !vbase )
        vbase = INVALID_PADDR;
    else
        vsize = GUEST_GICC_SIZE;

}
#else
static void __init gicv3_acpi_init(void) { }
#endif

static bool gic_dist_supports_lpis(void)
{
    return (readl_relaxed(GICD + GICD_TYPER) & GICD_TYPE_LPIS);
}

/* Set up the GIC */
extern int prtos_kernel_run;
static int __init gicv3_init(void)
{
    int res, i;
    uint32_t reg;
    unsigned int intid_bits;

    if ( !cpu_has_gicv3 )
    {
        dprintk(XENLOG_ERR, "GICv3: driver requires system register support\n");
        return -ENODEV;
    }

    if ( acpi_disabled )
        gicv3_dt_init();
    else
        gicv3_acpi_init();

    reg = readl_relaxed(GICD + GICD_PIDR2) & GIC_PIDR2_ARCH_MASK;
    if ( reg != GIC_PIDR2_ARCH_GICv3 && reg != GIC_PIDR2_ARCH_GICv4 )
         panic("GICv3: no distributor detected\n");

    for ( i = 0; i < gicv3.rdist_count; i++ )
    {
        /* map dbase & rdist regions */
        gicv3.rdist_regions[i].map_base =
                ioremap_nocache(gicv3.rdist_regions[i].base,
                                gicv3.rdist_regions[i].size);

        if ( !gicv3.rdist_regions[i].map_base )
            panic("GICv3: Failed to ioremap rdist region for region %d\n", i);
    }

    printk("GICv3 initialization:\n"
           "      gic_dist_addr=%#"PRIpaddr"\n"
           "      gic_maintenance_irq=%u\n"
           "      gic_rdist_stride=%#x\n"
           "      gic_rdist_regions=%d\n",
           dbase, gicv3_info.maintenance_irq,
           gicv3.rdist_stride, gicv3.rdist_count);
    printk("      redistributor regions:\n");
    for ( i = 0; i < gicv3.rdist_count; i++ )
    {
        const struct rdist_region *r = &gicv3.rdist_regions[i];

        printk("        - region %u: %#"PRIpaddr" - %#"PRIpaddr"\n",
               i, r->base, r->base + r->size);
    }

    reg = readl_relaxed(GICD + GICD_TYPER);
    intid_bits = GICD_TYPE_ID_BITS(reg);

    vgic_v3_setup_hw(dbase, gicv3.rdist_count, gicv3.rdist_regions, intid_bits);
    //gicv3_init_v2();

    spin_lock_init(&gicv3.lock);

    spin_lock(&gicv3.lock);

    gicv3_dist_init();

    if ( gic_dist_supports_lpis() )
    {
        res = gicv3_its_init();
        if ( res )
            panic("GICv3: ITS: initialization failed: %d\n", res);
    }

    res = gicv3_cpu_init();
    if ( res )
        goto out;

    gicv3_hyp_init();

out:
    spin_unlock(&gicv3.lock);

    return res;
}

static int __init gicv3_init_prtos(void)
{
    int res, i;
    uint32_t reg;
    unsigned int intid_bits;

    // if ( !cpu_has_gicv3 )
    // {
    //     dprintk(XENLOG_ERR, "GICv3: driver requires system register support\n");
    //     return -ENODEV;
    // }

    // if ( acpi_disabled )
        gicv3_dt_init_prtos();
    // else
        // gicv3_acpi_init();

    reg = readl_relaxed(GICD + GICD_PIDR2) & GIC_PIDR2_ARCH_MASK;
    if ( reg != GIC_PIDR2_ARCH_GICv3 && reg != GIC_PIDR2_ARCH_GICv4 )
         panic("GICv3: no distributor detected\n");

    for ( i = 0; i < gicv3.rdist_count; i++ )
    {
        /* map dbase & rdist regions */
        gicv3.rdist_regions[i].map_base =
                ioremap_nocache(gicv3.rdist_regions[i].base,
                                gicv3.rdist_regions[i].size);

        if ( !gicv3.rdist_regions[i].map_base )
            panic("GICv3: Failed to ioremap rdist region for region %d\n", i);
    }

    printk("GICv3 initialization:\n"
           "      gic_dist_addr=%#"PRIpaddr"\n"
           "      gic_maintenance_irq=%u\n"
           "      gic_rdist_stride=%#x\n"
           "      gic_rdist_regions=%d\n",
           dbase, gicv3_info.maintenance_irq,
           gicv3.rdist_stride, gicv3.rdist_count);
    printk("      redistributor regions:\n");
    for ( i = 0; i < gicv3.rdist_count; i++ )
    {
        const struct rdist_region *r = &gicv3.rdist_regions[i];

        printk("        - region %u: %#"PRIpaddr" - %#"PRIpaddr"\n",
               i, r->base, r->base + r->size);
    }

    reg = readl_relaxed(GICD + GICD_TYPER);
    intid_bits = GICD_TYPE_ID_BITS(reg);

    vgic_v3_setup_hw(dbase, gicv3.rdist_count, gicv3.rdist_regions, intid_bits);
    //gicv3_init_v2();

    spin_lock_init(&gicv3.lock);

    spin_lock(&gicv3.lock);

    gicv3_dist_init();

    /* Enable physical SPIs assigned to guest partitions via static IRQ
     * routing (e.g. UART PL011 SPI 1 = INTID 33 for Linux hw-virt). */
    prtos_gicv3_enable_spi(33);

    if ( gic_dist_supports_lpis() )
    {
        res = gicv3_its_init();
        if ( res )
            panic("GICv3: ITS: initialization failed: %d\n", res);
    }

    res = gicv3_cpu_init();
    if ( res )
        goto out;

    gicv3_hyp_init();

out:
    spin_unlock(&gicv3.lock);

    return res;
}

static const struct gic_hw_operations gicv3_ops = {
    .info                = &gicv3_info,
    .init                = gicv3_init,
    .save_state          = gicv3_save_state,
    .restore_state       = gicv3_restore_state,
    .dump_state          = gicv3_dump_state,
    .gic_host_irq_type   = &gicv3_host_irq_type,
    .gic_guest_irq_type  = &gicv3_guest_irq_type,
    .eoi_irq             = gicv3_eoi_irq,
    .deactivate_irq      = gicv3_dir_irq,
    .read_irq            = gicv3_read_irq,
    .set_active_state    = gicv3_set_active_state,
    .set_pending_state   = gicv3_set_pending_state,
    .set_irq_type        = gicv3_set_irq_type,
    .set_irq_priority    = gicv3_set_irq_priority,
    .send_SGI            = gicv3_send_sgi,
    .disable_interface   = gicv3_disable_interface,
    .update_lr           = gicv3_update_lr,
    .update_hcr_status   = gicv3_hcr_status,
    .clear_lr            = gicv3_clear_lr,
    .read_lr             = gicv3_read_lr,
    .write_lr            = gicv3_write_lr,
    .read_vmcr_priority  = gicv3_read_vmcr_priority,
    .read_apr            = gicv3_read_apr,
    .read_pending_state  = gicv3_read_pending_state,
    .secondary_init      = gicv3_secondary_cpu_init,
    .make_hwdom_dt_node  = gicv3_make_hwdom_dt_node,
#ifdef CONFIG_ACPI
    .make_hwdom_madt     = gicv3_make_hwdom_madt,
    .get_hwdom_extra_madt_size = gicv3_get_hwdom_extra_madt_size,
#endif
    .iomem_deny_access   = gicv3_iomem_deny_access,
    .do_LPI              = gicv3_do_LPI,
};

static const struct gic_hw_operations gicv3_ops_prtos = {
    .info                = &gicv3_info,
    .init                = gicv3_init_prtos,
    .save_state          = gicv3_save_state,
    .restore_state       = gicv3_restore_state,
    .dump_state          = gicv3_dump_state,
    .gic_host_irq_type   = &gicv3_host_irq_type,
    .gic_guest_irq_type  = &gicv3_guest_irq_type,
    .eoi_irq             = gicv3_eoi_irq,
    .deactivate_irq      = gicv3_dir_irq,
    .read_irq            = gicv3_read_irq,
    .set_active_state    = gicv3_set_active_state,
    .set_pending_state   = gicv3_set_pending_state,
    .set_irq_type        = gicv3_set_irq_type,
    .set_irq_priority    = gicv3_set_irq_priority,
    .send_SGI            = gicv3_send_sgi,
    .disable_interface   = gicv3_disable_interface,
    .update_lr           = gicv3_update_lr,
    .update_hcr_status   = gicv3_hcr_status,
    .clear_lr            = gicv3_clear_lr,
    .read_lr             = gicv3_read_lr,
    .write_lr            = gicv3_write_lr,
    .read_vmcr_priority  = gicv3_read_vmcr_priority,
    .read_apr            = gicv3_read_apr,
    .read_pending_state  = gicv3_read_pending_state,
    .secondary_init      = gicv3_secondary_cpu_init,
    .make_hwdom_dt_node  = gicv3_make_hwdom_dt_node,
#ifdef CONFIG_ACPI
    .make_hwdom_madt     = gicv3_make_hwdom_madt,
    .get_hwdom_extra_madt_size = gicv3_get_hwdom_extra_madt_size,
#endif
    .iomem_deny_access   = gicv3_iomem_deny_access,
    .do_LPI              = gicv3_do_LPI,
};

static int __init gicv3_dt_preinit(struct dt_device_node *node, const void *data)
{
    gicv3_info.hw_version = GIC_V3;
    gicv3_info.node = node;
    register_gic_ops(&gicv3_ops);
    dt_irq_xlate = gic_irq_xlate;

    return 0;
}

void gicv3_dt_preinit_prtos(void)
{
    gicv3_info.hw_version = GIC_V3;
    gicv3_info.node = NULL;
    register_gic_ops(&gicv3_ops_prtos);
    dt_irq_xlate = gic_irq_xlate;

}

static const struct dt_device_match gicv3_dt_match[] __initconst =
{
    DT_MATCH_GIC_V3,
    { /* sentinel */ },
};

DT_DEVICE_START(gicv3, "GICv3", DEVICE_INTERRUPT_CONTROLLER)
        .dt_match = gicv3_dt_match,
        .init = gicv3_dt_preinit,
DT_DEVICE_END

#ifdef CONFIG_ACPI
/* Set up the GIC */
static int __init gicv3_acpi_preinit(const void *data)
{
    gicv3_info.hw_version = GIC_V3;
    register_gic_ops(&gicv3_ops);

    return 0;
}

ACPI_DEVICE_START(agicv3, "GICv3", DEVICE_INTERRUPT_CONTROLLER)
        .class_type = ACPI_MADT_GIC_VERSION_V3,
        .init = gicv3_acpi_preinit,
ACPI_DEVICE_END

ACPI_DEVICE_START(agicv4, "GICv4", DEVICE_INTERRUPT_CONTROLLER)
        .class_type = ACPI_MADT_GIC_VERSION_V4,
        .init = gicv3_acpi_preinit,
ACPI_DEVICE_END
#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: gic-v3.c === */
/* === BEGIN INLINED: gic-vgic.c === */
#include <xen_xen_config.h>
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/gic-vgic.c
 *
 * ARM Generic Interrupt Controller virtualization support
 *
 * Tim Deegan <tim@xen.org>
 * Copyright (c) 2011 Citrix Systems.
 */

#include <xen_xen_config.h>

#include <xen_errno.h>
#include <xen_irq.h>
#include <xen_lib.h>
#include <xen_sched.h>
#include <asm_domain.h>
#include <asm_gic.h>
#include <asm_vgic.h>

#define lr_all_full() (this_cpu(lr_mask) == ((1 << gic_get_nr_lrs()) - 1))

#undef GIC_DEBUG

static void gic_update_one_lr(struct vcpu *v, int i);

static inline void gic_set_lr(int lr, struct pending_irq *p,
                              unsigned int state)
{
    ASSERT(!local_irq_is_enabled());

    clear_bit(GIC_IRQ_GUEST_PRISTINE_LPI, &p->status);

    gic_hw_ops->update_lr(lr, p->irq, p->priority,
                          p->desc ? p->desc->irq : INVALID_IRQ, state);

    set_bit(GIC_IRQ_GUEST_VISIBLE, &p->status);
    clear_bit(GIC_IRQ_GUEST_QUEUED, &p->status);
    p->lr = lr;
}

static inline void gic_add_to_lr_pending(struct vcpu *v, struct pending_irq *n)
{
    struct pending_irq *iter;

    ASSERT(spin_is_locked(&v->arch.vgic.lock));

    if ( !list_empty(&n->lr_queue) )
        return;

    list_for_each_entry ( iter, &v->arch.vgic.lr_pending, lr_queue )
    {
        if ( iter->priority > n->priority )
        {
            list_add_tail(&n->lr_queue, &iter->lr_queue);
            return;
        }
    }
    list_add_tail(&n->lr_queue, &v->arch.vgic.lr_pending);
}

void gic_remove_from_lr_pending(struct vcpu *v, struct pending_irq *p)
{
    ASSERT(spin_is_locked(&v->arch.vgic.lock));

    list_del_init(&p->lr_queue);
}

void gic_raise_inflight_irq(struct vcpu *v, unsigned int virtual_irq)
{
    struct pending_irq *n = irq_to_pending(v, virtual_irq);

    /* If an LPI has been removed meanwhile, there is nothing left to raise. */
    if ( unlikely(!n) )
        return;

    ASSERT(spin_is_locked(&v->arch.vgic.lock));

    /* Don't try to update the LR if the interrupt is disabled */
    if ( !test_bit(GIC_IRQ_GUEST_ENABLED, &n->status) )
        return;

    if ( list_empty(&n->lr_queue) )
    {
        if ( v == current )
            gic_update_one_lr(v, n->lr);
    }
#ifdef GIC_DEBUG
    else
        gdprintk(XENLOG_DEBUG, "trying to inject irq=%u into %pv, when it is still lr_pending\n",
                 virtual_irq, v);
#endif
}

/*
 * Find an unused LR to insert an IRQ into, starting with the LR given
 * by @lr. If this new interrupt is a PRISTINE LPI, scan the other LRs to
 * avoid inserting the same IRQ twice. This situation can occur when an
 * event gets discarded while the LPI is in an LR, and a new LPI with the
 * same number gets mapped quickly afterwards.
 */
static unsigned int gic_find_unused_lr(struct vcpu *v,
                                       struct pending_irq *p,
                                       unsigned int lr)
{
    unsigned int nr_lrs = gic_get_nr_lrs();
    unsigned long *lr_mask = (unsigned long *) &this_cpu(lr_mask);
    struct gic_lr lr_val;

    ASSERT(spin_is_locked(&v->arch.vgic.lock));

    if ( unlikely(test_bit(GIC_IRQ_GUEST_PRISTINE_LPI, &p->status)) )
    {
        unsigned int used_lr;

        for_each_set_bit(used_lr, lr_mask, nr_lrs)
        {
            gic_hw_ops->read_lr(used_lr, &lr_val);
            if ( lr_val.virq == p->irq )
                return used_lr;
        }
    }

    lr = find_next_zero_bit(lr_mask, nr_lrs, lr);

    return lr;
}

void gic_raise_guest_irq(struct vcpu *v, unsigned int virtual_irq,
        unsigned int priority)
{
    int i;
    unsigned int nr_lrs = gic_get_nr_lrs();
    struct pending_irq *p = irq_to_pending(v, virtual_irq);

    ASSERT(spin_is_locked(&v->arch.vgic.lock));

    if ( unlikely(!p) )
        /* An unmapped LPI does not need to be raised. */
        return;

    if ( v == current && list_empty(&v->arch.vgic.lr_pending) )
    {
        i = gic_find_unused_lr(v, p, 0);

        if (i < nr_lrs) {
            set_bit(i, &this_cpu(lr_mask));
            gic_set_lr(i, p, GICH_LR_PENDING);
            return;
        }
    }

    gic_add_to_lr_pending(v, p);
}

static void gic_update_one_lr(struct vcpu *v, int i)
{
    struct pending_irq *p;
    int irq;
    struct gic_lr lr_val;

    ASSERT(spin_is_locked(&v->arch.vgic.lock));
    ASSERT(!local_irq_is_enabled());

    gic_hw_ops->read_lr(i, &lr_val);
    irq = lr_val.virq;
    p = irq_to_pending(v, irq);
    /*
     * An LPI might have been unmapped, in which case we just clean up here.
     * If that LPI is marked as PRISTINE, the information in the LR is bogus,
     * as it belongs to a previous, already unmapped LPI. So we discard it
     * here as well.
     */
    if ( unlikely(!p ||
                  test_and_clear_bit(GIC_IRQ_GUEST_PRISTINE_LPI, &p->status)) )
    {
        ASSERT(is_lpi(irq));

        gic_hw_ops->clear_lr(i);
        clear_bit(i, &this_cpu(lr_mask));

        return;
    }

    if ( lr_val.active )
    {
        set_bit(GIC_IRQ_GUEST_ACTIVE, &p->status);
        if ( test_bit(GIC_IRQ_GUEST_ENABLED, &p->status) &&
             test_and_clear_bit(GIC_IRQ_GUEST_QUEUED, &p->status) )
        {
            if ( p->desc == NULL )
            {
                lr_val.pending = true;
                gic_hw_ops->write_lr(i, &lr_val);
            }
            else
                gdprintk(XENLOG_WARNING, "unable to inject hw irq=%d into %pv: already active in LR%d\n",
                         irq, v, i);
        }
    }
    else if ( lr_val.pending )
    {
        int q __attribute__ ((unused)) = test_and_clear_bit(GIC_IRQ_GUEST_QUEUED, &p->status);
#ifdef GIC_DEBUG
        if ( q )
            gdprintk(XENLOG_DEBUG, "trying to inject irq=%d into %pv, when it is already pending in LR%d\n",
                    irq, v, i);
#endif
    }
    else
    {
#ifndef NDEBUG
        gic_hw_ops->clear_lr(i);
#endif
        clear_bit(i, &this_cpu(lr_mask));

        if ( p->desc != NULL )
            clear_bit(_IRQ_INPROGRESS, &p->desc->status);
        clear_bit(GIC_IRQ_GUEST_VISIBLE, &p->status);
        clear_bit(GIC_IRQ_GUEST_ACTIVE, &p->status);
        p->lr = GIC_INVALID_LR;
        if ( test_bit(GIC_IRQ_GUEST_ENABLED, &p->status) &&
             test_bit(GIC_IRQ_GUEST_QUEUED, &p->status) &&
             !test_bit(GIC_IRQ_GUEST_MIGRATING, &p->status) )
            gic_raise_guest_irq(v, irq, p->priority);
        else {
            list_del_init(&p->inflight);
            /*
             * Remove from inflight, then change physical affinity. It
             * makes sure that when a new interrupt is received on the
             * next pcpu, inflight is already cleared. No concurrent
             * accesses to inflight.
             */
            smp_wmb();
            if ( test_bit(GIC_IRQ_GUEST_MIGRATING, &p->status) )
            {
                struct vcpu *v_target = vgic_get_target_vcpu(v, irq);
                irq_set_affinity(p->desc, cpumask_of(v_target->processor));
                clear_bit(GIC_IRQ_GUEST_MIGRATING, &p->status);
            }
        }
    }
}

void vgic_sync_from_lrs(struct vcpu *v)
{
    int i = 0;
    unsigned long flags;
    unsigned int nr_lrs = gic_get_nr_lrs();

    /* The idle domain has no LRs to be cleared. Since gic_restore_state
     * doesn't write any LR registers for the idle domain they could be
     * non-zero. */
    if ( is_idle_vcpu(v) )
        return;

    gic_hw_ops->update_hcr_status(GICH_HCR_UIE, false);

    spin_lock_irqsave(&v->arch.vgic.lock, flags);

    while ((i = find_next_bit((const unsigned long *) &this_cpu(lr_mask),
                              nr_lrs, i)) < nr_lrs ) {
        gic_update_one_lr(v, i);
        i++;
    }

    spin_unlock_irqrestore(&v->arch.vgic.lock, flags);
}

static void gic_restore_pending_irqs(struct vcpu *v)
{
    int lr = 0;
    struct pending_irq *p, *t, *p_r;
    struct list_head *inflight_r;
    unsigned int nr_lrs = gic_get_nr_lrs();
    int lrs = nr_lrs;

    ASSERT(!local_irq_is_enabled());

    spin_lock(&v->arch.vgic.lock);

    if ( list_empty(&v->arch.vgic.lr_pending) )
        goto out;

    inflight_r = &v->arch.vgic.inflight_irqs;
    list_for_each_entry_safe ( p, t, &v->arch.vgic.lr_pending, lr_queue )
    {
        lr = gic_find_unused_lr(v, p, lr);
        if ( lr >= nr_lrs )
        {
            /* No more free LRs: find a lower priority irq to evict */
            list_for_each_entry_reverse( p_r, inflight_r, inflight )
            {
                if ( p_r->priority == p->priority )
                    goto out;
                if ( test_bit(GIC_IRQ_GUEST_VISIBLE, &p_r->status) &&
                     !test_bit(GIC_IRQ_GUEST_ACTIVE, &p_r->status) )
                    goto found;
            }
            /* We didn't find a victim this time, and we won't next
             * time, so quit */
            goto out;

found:
            lr = p_r->lr;
            p_r->lr = GIC_INVALID_LR;
            set_bit(GIC_IRQ_GUEST_QUEUED, &p_r->status);
            clear_bit(GIC_IRQ_GUEST_VISIBLE, &p_r->status);
            gic_add_to_lr_pending(v, p_r);
            inflight_r = &p_r->inflight;
        }

        gic_set_lr(lr, p, GICH_LR_PENDING);
        list_del_init(&p->lr_queue);
        set_bit(lr, &this_cpu(lr_mask));

        /* We can only evict nr_lrs entries */
        lrs--;
        if ( lrs == 0 )
            break;
    }

out:
    spin_unlock(&v->arch.vgic.lock);
}

void gic_clear_pending_irqs(struct vcpu *v)
{
    struct pending_irq *p, *t;

    ASSERT(spin_is_locked(&v->arch.vgic.lock));

    v->arch.lr_mask = 0;
    list_for_each_entry_safe ( p, t, &v->arch.vgic.lr_pending, lr_queue )
        gic_remove_from_lr_pending(v, p);
}

/**
 * vgic_vcpu_pending_irq() - determine if interrupts need to be injected
 * @vcpu: The vCPU on which to check for interrupts.
 *
 * Checks whether there is an interrupt on the given VCPU which needs
 * handling in the guest. This requires at least one IRQ to be pending
 * and enabled.
 *
 * Returns: 1 if the guest should run to handle interrupts, 0 otherwise.
 */
int vgic_vcpu_pending_irq(struct vcpu *v)
{
    struct pending_irq *p;
    unsigned long flags;
    const unsigned long apr = gic_hw_ops->read_apr(0);
    int mask_priority;
    int active_priority;
    int rc = 0;

    /* We rely on reading the VMCR, which is only accessible locally. */
    ASSERT(v == current);

    mask_priority = gic_hw_ops->read_vmcr_priority();
    active_priority = find_first_bit(&apr, 32);

    spin_lock_irqsave(&v->arch.vgic.lock, flags);

    /* TODO: We order the guest irqs by priority, but we don't change
     * the priority of host irqs. */

    /* find the first enabled non-active irq, the queue is already
     * ordered by priority */
    list_for_each_entry( p, &v->arch.vgic.inflight_irqs, inflight )
    {
        if ( GIC_PRI_TO_GUEST(p->priority) >= mask_priority )
            goto out;
        if ( GIC_PRI_TO_GUEST(p->priority) >= active_priority )
            goto out;
        if ( test_bit(GIC_IRQ_GUEST_ENABLED, &p->status) )
        {
            rc = 1;
            goto out;
        }
    }

out:
    spin_unlock_irqrestore(&v->arch.vgic.lock, flags);
    return rc;
}

void vgic_sync_to_lrs(void)
{
    ASSERT(!local_irq_is_enabled());

    gic_restore_pending_irqs(current);

    if ( !list_empty(&current->arch.vgic.lr_pending) && lr_all_full() )
        gic_hw_ops->update_hcr_status(GICH_HCR_UIE, true);
}

void gic_dump_vgic_info(struct vcpu *v)
{
    struct pending_irq *p;

    list_for_each_entry ( p, &v->arch.vgic.inflight_irqs, inflight )
        printk("Inflight irq=%u lr=%u\n", p->irq, p->lr);

    list_for_each_entry( p, &v->arch.vgic.lr_pending, lr_queue )
        printk("Pending irq=%d\n", p->irq);
}

struct irq_desc *vgic_get_hw_irq_desc(struct domain *d, struct vcpu *v,
                                      unsigned int virq)
{
    struct pending_irq *p;

    ASSERT(!v && virq >= 32);

    if ( !v )
        v = d->vcpu[0];

    p = irq_to_pending(v, virq);
    if ( !p )
        return NULL;

    return p->desc;
}

int vgic_connect_hw_irq(struct domain *d, struct vcpu *v, unsigned int virq,
                        struct irq_desc *desc, bool connect)
{
    unsigned long flags;
    /*
     * Use vcpu0 to retrieve the pending_irq struct. Given that we only
     * route SPIs to guests, it doesn't make any difference.
     */
    struct vcpu *v_target = vgic_get_target_vcpu(d->vcpu[0], virq);
    struct vgic_irq_rank *rank = vgic_rank_irq(v_target, virq);
    struct pending_irq *p = irq_to_pending(v_target, virq);
    int ret = 0;

    /* "desc" is optional when we disconnect an IRQ. */
    ASSERT(!connect || desc);

    /* We are taking to rank lock to prevent parallel connections. */
    vgic_lock_rank(v_target, rank, flags);

    if ( connect )
    {
        /*
         * The VIRQ should not be already enabled by the guest nor
         * active/pending in the guest.
         */
        if ( !p->desc &&
             !test_bit(GIC_IRQ_GUEST_ENABLED, &p->status) &&
             !test_bit(GIC_IRQ_GUEST_VISIBLE, &p->status) &&
             !test_bit(GIC_IRQ_GUEST_ACTIVE, &p->status) )
            p->desc = desc;
        else
            ret = -EBUSY;
    }
    else
    {
        if ( desc && p->desc != desc )
            ret = -EINVAL;
        else
            p->desc = NULL;
    }

    vgic_unlock_rank(v_target, rank, flags);

    return ret;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: gic-vgic.c === */
/* === BEGIN INLINED: irq.c === */
#include <xen_xen_config.h>
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/irq.c
 *
 * ARM Interrupt support
 *
 * Ian Campbell <ian.campbell@citrix.com>
 * Copyright (c) 2011 Citrix Systems.
 */
#include <xen_xen_config.h>
#include <xen_cpu.h>
#include <xen_lib.h>
#include <xen_spinlock.h>
#include <xen_irq.h>
#include <xen_init.h>
#include <xen_errno.h>
#include <xen_sched.h>

#include <asm_gic.h>
#include <asm_vgic.h>

const unsigned int nr_irqs = NR_IRQS;

static unsigned int local_irqs_type[NR_LOCAL_IRQS];
static DEFINE_SPINLOCK(local_irqs_type_lock);

/* Describe an IRQ assigned to a guest */
struct irq_guest
{
    struct domain *d;
    unsigned int virq;
};

void irq_ack_none(struct irq_desc *desc)
{
    printk("unexpected IRQ trap at irq %02x\n", desc->irq);
}

void irq_end_none(struct irq_desc *irq)
{
    /*
     * Still allow a CPU to end an interrupt if we receive a spurious
     * interrupt. This will prevent the CPU to lose interrupt forever.
     */
    gic_hw_ops->gic_host_irq_type->end(irq);
}

static irq_desc_t irq_desc[NR_IRQS];
static DEFINE_PER_CPU(irq_desc_t[NR_LOCAL_IRQS], local_irq_desc);

struct irq_desc *__irq_to_desc(int irq)
{
    if ( irq < NR_LOCAL_IRQS )
        return &this_cpu(local_irq_desc)[irq];

    return &irq_desc[irq-NR_LOCAL_IRQS];
}

int arch_init_one_irq_desc(struct irq_desc *desc)
{
    desc->arch.type = IRQ_TYPE_INVALID;
    return 0;
}


static int __init init_irq_data(void)
{
    int irq;

    for ( irq = NR_LOCAL_IRQS; irq < NR_IRQS; irq++ )
    {
        struct irq_desc *desc = irq_to_desc(irq);
        int rc = init_one_irq_desc(desc);

        if ( rc )
            return rc;

        desc->irq = irq;
        desc->action  = NULL;
    }

    return 0;
}

static int init_local_irq_data(unsigned int cpu)
{
    int irq;

    spin_lock(&local_irqs_type_lock);

    for ( irq = 0; irq < NR_LOCAL_IRQS; irq++ )
    {
        struct irq_desc *desc = &per_cpu(local_irq_desc, cpu)[irq];
        int rc = init_one_irq_desc(desc);

        if ( rc )
            return rc;

        desc->irq = irq;
        desc->action  = NULL;

        /* PPIs are included in local_irqs, we copy the IRQ type from
         * local_irqs_type when bringing up local IRQ for this CPU in
         * order to pick up any configuration done before this CPU came
         * up. For interrupts configured after this point this is done in
         * irq_set_type.
         */
        desc->arch.type = local_irqs_type[irq];
    }

    spin_unlock(&local_irqs_type_lock);

    return 0;
}

static int cpu_callback(struct notifier_block *nfb, unsigned long action,
                        void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;
    int rc = 0;

    switch ( action )
    {
    case CPU_UP_PREPARE:
        rc = init_local_irq_data(cpu);
        if ( rc )
            printk(XENLOG_ERR "Unable to allocate local IRQ for CPU%u\n",
                   cpu);
        break;
    }

    return notifier_from_errno(rc);
}

int init_local_irq_data_prtos(unsigned int cpu) {
    return init_local_irq_data(cpu);
}

static struct notifier_block cpu_nfb = {
    .notifier_call = cpu_callback,
};

void __init init_IRQ(void)
{
    int irq;

    spin_lock(&local_irqs_type_lock);
    for ( irq = 0; irq < NR_LOCAL_IRQS; irq++ )
    {
        /* SGIs are always edge-triggered */
        if ( irq < NR_GIC_SGI )
            local_irqs_type[irq] = IRQ_TYPE_EDGE_RISING;
        else
            local_irqs_type[irq] = IRQ_TYPE_INVALID;
    }
    spin_unlock(&local_irqs_type_lock);

    BUG_ON(init_local_irq_data(smp_processor_id()) < 0);
    BUG_ON(init_irq_data() < 0);

    register_cpu_notifier(&cpu_nfb);
}

static inline struct irq_guest *irq_get_guest_info(struct irq_desc *desc)
{
    ASSERT(spin_is_locked(&desc->lock));
    ASSERT(test_bit(_IRQ_GUEST, &desc->status));
    ASSERT(desc->action != NULL);

    return desc->action->dev_id;
}

static inline struct domain *irq_get_domain(struct irq_desc *desc)
{
    return irq_get_guest_info(desc)->d;
}

void irq_set_affinity(struct irq_desc *desc, const cpumask_t *mask)
{
    if ( desc != NULL )
        desc->handler->set_affinity(desc, mask);
}
static struct irqaction local_irqaction[20];
static int  irqaction_index = 0;
int request_irq(unsigned int irq, unsigned int irqflags,
                void (*handler)(int irq, void *dev_id),
                const char *devname, void *dev_id)
{
    struct irqaction *action;
    int retval;

    /*
     * Sanity-check: shared interrupts must pass in a real dev-ID,
     * otherwise we'll have trouble later trying to figure out
     * which interrupt is which (messes up the interrupt freeing
     * logic etc).
     */
    if ( irq >= nr_irqs )
        return -EINVAL;
    if ( !handler )
        return -EINVAL;

    // action = xmalloc(struct irqaction);
    // if ( !action )
    //     return -ENOMEM;
    action = &local_irqaction[irqaction_index++]; // Use the static action for PRToS
    printk("PRTOS request_irq: irqaction_index: %d\n", irqaction_index);
    if(irqaction_index > 20) {
        printk("PRTOS request_irq: Too many IRQs requested, increase the size of local_irqaction array\n");
        return -ENOMEM;
    }

    action->handler = handler;
    action->name = devname;
    action->dev_id = dev_id;
    action->free_on_release = 1;

    retval = setup_irq(irq, irqflags, action);
    if ( retval )
        xfree(action);

    return retval;
}

struct irqaction irqaction_prtos[100];
static int index = 0;
int request_irq_prtos(unsigned int irq, unsigned int irqflags,
                void (*handler)(int irq, void *dev_id),
                const char *devname, void *dev_id)
{
    struct irqaction *action;
    int retval;

    /*
     * Sanity-check: shared interrupts must pass in a real dev-ID,
     * otherwise we'll have trouble later trying to figure out
     * which interrupt is which (messes up the interrupt freeing
     * logic etc).
     */
    if ( irq >= nr_irqs )
        return -EINVAL;
    if ( !handler )
        return -EINVAL;

    // action = xmalloc(struct irqaction);
    // if ( !action )
    //     return -ENOMEM;
    action = &irqaction_prtos[index++]; // Use the static action for PRToS
    if(index >100) {
        printk("PRToS: Too many IRQs requested, increase the size of irqaction_prtos array\n");
        return -ENOMEM;
    }
    action->handler = handler;
    action->name = devname;
    action->dev_id = dev_id;
    action->free_on_release = 1;

    retval = setup_irq(irq, irqflags, action);
    if ( retval )
        xfree(action);

    return retval;
}

extern int prtos_kernel_run;
/* Dispatch an interrupt */
void do_IRQ(struct cpu_user_regs *regs, unsigned int irq, int is_fiq)
{
    struct irq_desc *desc = irq_to_desc(irq);
    struct irqaction *action;
    const struct cpu_user_regs *old_regs = set_irq_regs(regs);

    unsigned int cpu = smp_processor_id();
    perfc_incr(irqs);

    /* Statically assigned SGIs do not come down this path */
    ASSERT(irq >= GIC_SGI_STATIC_MAX);

    if ( irq < NR_GIC_SGI )
         perfc_incr(ipis);
     else if ( irq < NR_GIC_LOCAL_IRQS )
         perfc_incr(ppis);
    else
         perfc_incr(spis);

    /* TODO: this_cpu(irq_count)++; */

    irq_enter();

    spin_lock(&desc->lock);
    desc->handler->ack(desc);

#ifndef NDEBUG
    if ( !desc->action )
    {
        printk("Unknown %s %#3.3x\n",
               is_fiq ? "FIQ" : "IRQ", irq);
        goto out;
    }
#endif

    if ( test_bit(_IRQ_GUEST, &desc->status) )
    {
        struct irq_guest *info = irq_get_guest_info(desc);

        perfc_incr(guest_irqs);
        desc->handler->end(desc);

        set_bit(_IRQ_INPROGRESS, &desc->status);

        /*
         * The irq cannot be a PPI, we only support delivery of SPIs to
         * guests.
         */
        ASSERT(irq >= NR_GIC_SGI);
        vgic_inject_irq(info->d, NULL, info->virq, true);
        goto out_no_end;
    }

    if ( test_bit(_IRQ_DISABLED, &desc->status) )
        goto out;

    set_bit(_IRQ_INPROGRESS, &desc->status);

    action = desc->action;

    spin_unlock_irq(&desc->lock);

    do
    {
        action->handler(irq, action->dev_id);
        action = action->next;
    } while ( action );

    spin_lock_irq(&desc->lock);

    clear_bit(_IRQ_INPROGRESS, &desc->status);

out:
    desc->handler->end(desc);
out_no_end:
    spin_unlock(&desc->lock);
    irq_exit();
    set_irq_regs(old_regs);

    // If the PRTOS kernel is running, we reprogram the timer to check the functionaliy of timer irq
    if (prtos_kernel_run) {
        /* Reprogram the timer */
        void enable_timer_prtos(void);
        enable_timer_prtos();
    }
}

void release_irq(unsigned int irq, const void *dev_id)
{
    struct irq_desc *desc;
    unsigned long flags;
    struct irqaction *action, **action_ptr;

    desc = irq_to_desc(irq);

    spin_lock_irqsave(&desc->lock,flags);

    action_ptr = &desc->action;
    for ( ;; )
    {
        action = *action_ptr;
        if ( !action )
        {
            printk(XENLOG_WARNING "Trying to free already-free IRQ %u\n", irq);
            spin_unlock_irqrestore(&desc->lock, flags);
            return;
        }

        if ( action->dev_id == dev_id )
            break;

        action_ptr = &action->next;
    }

    /* Found it - remove it from the action list */
    *action_ptr = action->next;

    /* If this was the last action, shut down the IRQ */
    if ( !desc->action )
    {
        desc->handler->shutdown(desc);
        clear_bit(_IRQ_GUEST, &desc->status);
    }

    spin_unlock_irqrestore(&desc->lock,flags);

    /* Wait to make sure it's not being used on another CPU */
    do { smp_mb(); } while ( test_bit(_IRQ_INPROGRESS, &desc->status) );

    if ( action->free_on_release )
        xfree(action);
}

static int __setup_irq(struct irq_desc *desc, unsigned int irqflags,
                       struct irqaction *new)
{
    bool shared = irqflags & IRQF_SHARED;

    ASSERT(new != NULL);

    /* Sanity checks:
     *  - if the IRQ is marked as shared
     *  - dev_id is not NULL when IRQF_SHARED is set
     */
    if ( desc->action != NULL && (!test_bit(_IRQF_SHARED, &desc->status) || !shared) )
        return -EINVAL;
    if ( shared && new->dev_id == NULL )
        return -EINVAL;

    if ( shared )
        set_bit(_IRQF_SHARED, &desc->status);

    new->next = desc->action;
    dsb(ish);
    desc->action = new;
    dsb(ish);

    return 0;
}

int setup_irq(unsigned int irq, unsigned int irqflags, struct irqaction *new)
{
    int rc;
    unsigned long flags;
    struct irq_desc *desc;
    bool disabled;

    desc = irq_to_desc(irq);

    spin_lock_irqsave(&desc->lock, flags);

    if ( test_bit(_IRQ_GUEST, &desc->status) )
    {
        struct domain *d = irq_get_domain(desc);

        spin_unlock_irqrestore(&desc->lock, flags);
        printk(XENLOG_ERR "ERROR: IRQ %u is already in use by the domain %u\n",
               irq, d->domain_id);
        return -EBUSY;
    }

    disabled = (desc->action == NULL);

    rc = __setup_irq(desc, irqflags, new);
    if ( rc )
        goto err;

    /* First time the IRQ is setup */
    if ( disabled )
    {
        printk("setup_irq: irq %d\n", irq);
        gic_route_irq_to_xen(desc, GIC_PRI_IRQ);
        /* It's fine to use smp_processor_id() because:
         * For SGI and PPI: irq_desc is banked
         * For SPI: we don't care for now which CPU will receive the
         * interrupt
         * TODO: Handle case where SPI is setup on different CPU than
         * the targeted CPU and the priority.
         */
        irq_set_affinity(desc, cpumask_of(smp_processor_id()));
        desc->handler->startup(desc);
    }

err:
    spin_unlock_irqrestore(&desc->lock, flags);

    return rc;
}

bool is_assignable_irq(unsigned int irq)
{
    /* For now, we can only route SPIs to the guest */
    return (irq >= NR_LOCAL_IRQS) && (irq < gic_number_lines());
}

/*
 * Only the hardware domain is allowed to set the configure the
 * interrupt type for now.
 *
 * XXX: See whether it is possible to let any domain configure the type.
 */
bool irq_type_set_by_domain(const struct domain *d)
{
    return is_hardware_domain(d);
}

/*
 * Route an IRQ to a specific guest.
 * For now only SPIs are assignable to the guest.
 */
int route_irq_to_guest(struct domain *d, unsigned int virq,
                       unsigned int irq, const char * devname)
{
    struct irqaction *action;
    struct irq_guest *info;
    struct irq_desc *desc;
    unsigned long flags;
    int retval = 0;

    if ( virq >= vgic_num_irqs(d) )
    {
        printk(XENLOG_G_ERR
               "the vIRQ number %u is too high for domain %u (max = %u)\n",
               irq, d->domain_id, vgic_num_irqs(d));
        return -EINVAL;
    }

    /* Only routing to virtual SPIs is supported */
    if ( virq < NR_LOCAL_IRQS )
    {
        printk(XENLOG_G_ERR "IRQ can only be routed to an SPI\n");
        return -EINVAL;
    }

    if ( !is_assignable_irq(irq) )
    {
        printk(XENLOG_G_ERR "the IRQ%u is not routable\n", irq);
        return -EINVAL;
    }
    desc = irq_to_desc(irq);

    action = xmalloc(struct irqaction);
    if ( !action )
        return -ENOMEM;

    info = xmalloc(struct irq_guest);
    if ( !info )
    {
        xfree(action);
        return -ENOMEM;
    }

    info->d = d;
    info->virq = virq;

    action->dev_id = info;
    action->name = devname;
    action->free_on_release = 1;

    spin_lock_irqsave(&desc->lock, flags);

    if ( !irq_type_set_by_domain(d) && desc->arch.type == IRQ_TYPE_INVALID )
    {
        printk(XENLOG_G_ERR "IRQ %u has not been configured\n", irq);
        retval = -EIO;
        goto out;
    }

    /*
     * If the IRQ is already used by someone
     *  - If it's the same domain -> Xen doesn't need to update the IRQ desc.
     *  For safety check if we are not trying to assign the IRQ to a
     *  different vIRQ.
     *  - Otherwise -> For now, don't allow the IRQ to be shared between
     *  Xen and domains.
     */
    if ( desc->action != NULL )
    {
        if ( test_bit(_IRQ_GUEST, &desc->status) )
        {
            struct domain *ad = irq_get_domain(desc);

            if ( d != ad )
            {
                printk(XENLOG_G_ERR "IRQ %u is already used by domain %u\n",
                       irq, ad->domain_id);
                retval = -EBUSY;
            }
            else if ( irq_get_guest_info(desc)->virq != virq )
            {
                printk(XENLOG_G_ERR
                       "d%u: IRQ %u is already assigned to vIRQ %u\n",
                       d->domain_id, irq, irq_get_guest_info(desc)->virq);
                retval = -EBUSY;
            }
        }
        else
        {
            printk(XENLOG_G_ERR "IRQ %u is already used by Xen\n", irq);
            retval = -EBUSY;
        }
        goto out;
    }

    retval = __setup_irq(desc, 0, action);
    if ( retval )
        goto out;

    retval = gic_route_irq_to_guest(d, virq, desc, GIC_PRI_IRQ);

    spin_unlock_irqrestore(&desc->lock, flags);

    if ( retval )
    {
        release_irq(desc->irq, info);
        goto free_info;
    }

    return 0;

out:
    spin_unlock_irqrestore(&desc->lock, flags);
    xfree(action);
free_info:
    xfree(info);

    return retval;
}

int release_guest_irq(struct domain *d, unsigned int virq)
{
    struct irq_desc *desc;
    struct irq_guest *info;
    unsigned long flags;
    int ret;

    /* Only SPIs are supported */
    if ( virq < NR_LOCAL_IRQS || virq >= vgic_num_irqs(d) )
        return -EINVAL;

    desc = vgic_get_hw_irq_desc(d, NULL, virq);
    if ( !desc )
        return -EINVAL;

    spin_lock_irqsave(&desc->lock, flags);

    ret = -EINVAL;
    if ( !test_bit(_IRQ_GUEST, &desc->status) )
        goto unlock;

    info = irq_get_guest_info(desc);
    ret = -EINVAL;
    if ( d != info->d )
        goto unlock;

    ret = gic_remove_irq_from_guest(d, virq, desc);
    if ( ret )
        goto unlock;

    spin_unlock_irqrestore(&desc->lock, flags);

    release_irq(desc->irq, info);
    xfree(info);

    return 0;

unlock:
    spin_unlock_irqrestore(&desc->lock, flags);

    return ret;
}




void pirq_set_affinity(struct domain *d, int pirq, const cpumask_t *mask)
{
    BUG();
}

static bool irq_validate_new_type(unsigned int curr, unsigned int new)
{
    return (curr == IRQ_TYPE_INVALID || curr == new );
}

int irq_set_spi_type(unsigned int spi, unsigned int type)
{
    unsigned long flags;
    struct irq_desc *desc = irq_to_desc(spi);
    int ret = -EBUSY;

    /* This function should not be used for other than SPIs */
    if ( spi < NR_LOCAL_IRQS )
        return -EINVAL;

    spin_lock_irqsave(&desc->lock, flags);

    if ( !irq_validate_new_type(desc->arch.type, type) )
        goto err;

    desc->arch.type = type;

    ret = 0;

err:
    spin_unlock_irqrestore(&desc->lock, flags);
    return ret;
}

static int irq_local_set_type(unsigned int irq, unsigned int type)
{
    unsigned int cpu;
    unsigned int old_type;
    unsigned long flags;
    int ret = -EBUSY;
    struct irq_desc *desc;

    ASSERT(irq < NR_LOCAL_IRQS);

    spin_lock(&local_irqs_type_lock);

    old_type = local_irqs_type[irq];

    if ( !irq_validate_new_type(old_type, type) )
        goto unlock;

    ret = 0;
    /* We don't need to reconfigure if the type is correctly set */
    if ( old_type == type )
        goto unlock;

    local_irqs_type[irq] = type;

    for_each_cpu( cpu, &cpu_online_map )
    {
        desc = &per_cpu(local_irq_desc, cpu)[irq];
        spin_lock_irqsave(&desc->lock, flags);
        desc->arch.type = type;
        spin_unlock_irqrestore(&desc->lock, flags);
    }

unlock:
    spin_unlock(&local_irqs_type_lock);
    return ret;
}

int irq_set_type(unsigned int irq, unsigned int type)
{
    int res;

    /* Setup the IRQ type */
    if ( irq < NR_LOCAL_IRQS )
        res = irq_local_set_type(irq, type);
    else
        res = irq_set_spi_type(irq, type);

    return res;
}

int platform_get_irq(const struct dt_device_node *device, int index)
{
    struct dt_irq dt_irq;
    unsigned int type, irq;

    if ( dt_device_get_irq(device, index, &dt_irq) )
        return -1;

    // dt_irq.irq = 0x21;
    // dt_irq.type = 0x4;;
    printk("platform_get_irq: dt_irq.irq: %u, dt_irq.type: %u\n", dt_irq.irq, dt_irq.type);

    irq = dt_irq.irq;
    type = dt_irq.type;

    if ( irq_set_type(irq, type) )
        return -1;

    return irq;
}

int platform_get_irq_byname(const struct dt_device_node *np, const char *name)
{
	int index;

	if ( unlikely(!name) )
		return -EINVAL;

	index = dt_property_match_string(np, "interrupt-names", name);
	if ( index < 0 )
		return index;

	return platform_get_irq(np, index);
}

#if CONFIG_STATIC_IRQ_ROUTING

/* ===============================
 * Static IRQ routing support
 * =============================== */

static struct static_irq_guest guest0 = {
    .id = 0,
};

static struct static_irq_map static_irq_map[] = {
    {26, NULL, STATIC_IRQ_HYP},     /* CNTHP EL2 timer */
    {33, NULL, STATIC_IRQ_VIRTUAL}, /* UART SPI */
};

#define NR_STATIC_IRQ_MAP (sizeof(static_irq_map) / sizeof(static_irq_map[0]))

struct static_irq_guest *static_irq_owner(uint32_t irq, enum static_irq_type *type) {
    unsigned int i;

    for (i = 0; i < NR_STATIC_IRQ_MAP; i++) {
        if (static_irq_map[i].irq == irq) {
            *type = static_irq_map[i].type;
            return static_irq_map[i].guest;
        }
    }

    *type = STATIC_IRQ_HYP;
    return NULL;
}

void static_vgic_inject(struct static_irq_guest *g, uint32_t irq) {
    extern int prtos_vgic_inject_hw_spi(uint32_t intid);
    (void)g;
    if (irq >= 32) {
        prtos_vgic_inject_hw_spi(irq);
    }
}

/*
 * Handle a physical SPI.
 * Returns 0 — caller should do full EOI+DIR (prtos_gicv3_host_irq_end).
 * The virtual interrupt is injected via software pending state and
 * delivered to the guest through vgic_flush_lrs on next ERET.
 */
int static_handle_spi(struct cpu_user_regs *regs, unsigned int irq) {
    struct static_irq_guest *g;
    enum static_irq_type type;

    g = static_irq_owner(irq, &type);

    switch (type) {
        case STATIC_IRQ_HYP:
            /* Xen internal IRQ: ignore for now */
            break;

        case STATIC_IRQ_DIRECT:
            /* Not implemented */
            break;

        case STATIC_IRQ_VIRTUAL:
            static_vgic_inject(g, irq);
            return 0;  /* Caller: full EOI+DIR, physical SPI is masked in inject */

        default:
            printk("static IRQ: bad type for irq %u\n", irq);
            break;
    }
    return 0;
}

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: irq.c === */
/* === BEGIN INLINED: vgic.c === */
#include <xen_xen_config.h>
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/vgic.c
 *
 * ARM Virtual Generic Interrupt Controller support
 *
 * Ian Campbell <ian.campbell@citrix.com>
 * Copyright (c) 2011 Citrix Systems.
 */

#include <xen_xen_config.h>

#include <xen_bitops.h>
#include <xen_lib.h>
#include <xen_init.h>
#include <xen_domain_page.h>
#include <xen_softirq.h>
#include <xen_irq.h>
#include <xen_sched.h>
#include <xen_perfc.h>

#include <asm_event.h>
#include <asm_current.h>

#include <asm_mmio.h>
#include <asm_gic.h>
#include <asm_vgic.h>

static inline struct vgic_irq_rank *vgic_get_rank(struct vcpu *v,
                                                  unsigned int rank)
{
    if ( rank == 0 )
        return v->arch.vgic.private_irqs;
    else if ( rank <= DOMAIN_NR_RANKS(v->domain) )
        return &v->domain->arch.vgic.shared_irqs[rank - 1];
    else
        return NULL;
}

/*
 * Returns rank corresponding to a GICD_<FOO><n> register for
 * GICD_<FOO> with <b>-bits-per-interrupt.
 */
struct vgic_irq_rank *vgic_rank_offset(struct vcpu *v, unsigned int b,
                                       unsigned int n, unsigned int s)
{
    unsigned int rank = REG_RANK_NR(b, (n >> s));

    return vgic_get_rank(v, rank);
}

struct vgic_irq_rank *vgic_rank_irq(struct vcpu *v, unsigned int irq)
{
    unsigned int rank = irq / 32;

    return vgic_get_rank(v, rank);
}

void vgic_init_pending_irq(struct pending_irq *p, unsigned int virq)
{
    /* The lpi_vcpu_id field must be big enough to hold a VCPU ID. */
    BUILD_BUG_ON(BIT(sizeof(p->lpi_vcpu_id) * 8, UL) < MAX_VIRT_CPUS);

    memset(p, 0, sizeof(*p));
    INIT_LIST_HEAD(&p->inflight);
    INIT_LIST_HEAD(&p->lr_queue);
    p->irq = virq;
    p->lpi_vcpu_id = INVALID_VCPU_ID;
}

static void vgic_rank_init(struct vgic_irq_rank *rank, uint8_t index,
                           unsigned int vcpu)
{
    unsigned int i;

    /*
     * Make sure that the type chosen to store the target is able to
     * store an VCPU ID between 0 and the maximum of virtual CPUs
     * supported.
     */
    BUILD_BUG_ON((1 << (sizeof(rank->vcpu[0]) * 8)) < MAX_VIRT_CPUS);

    spin_lock_init(&rank->lock);

    rank->index = index;

    for ( i = 0; i < NR_INTERRUPT_PER_RANK; i++ )
        write_atomic(&rank->vcpu[i], vcpu);
}

int domain_vgic_register(struct domain *d, unsigned int *mmio_count)
{
    switch ( d->arch.vgic.version )
    {
#ifdef CONFIG_GICV3
    case GIC_V3:
        if ( vgic_v3_init(d, mmio_count) )
           return -ENODEV;
        break;
#endif
// #ifdef CONFIG_VGICV2
// #endif
    default:
        printk(XENLOG_G_ERR "d%d: Unknown vGIC version %u\n",
               d->domain_id, d->arch.vgic.version);
        return -ENODEV;
    }

    return 0;
}

int domain_vgic_init(struct domain *d, unsigned int nr_spis)
{
    int i;
    int ret;

    d->arch.vgic.ctlr = 0;

    /*
     * The vGIC relies on having a pending_irq available for every IRQ
     * described in the ranks. As each rank describes 32 interrupts, we
     * need to make sure the number of SPIs is a multiple of 32.
     */
    nr_spis = ROUNDUP(nr_spis, 32);

    /* Limit the number of virtual SPIs supported to (1020 - 32) = 988  */
    if ( nr_spis > (1020 - NR_LOCAL_IRQS) )
        return -EINVAL;

    d->arch.vgic.nr_spis = nr_spis;

    spin_lock_init(&d->arch.vgic.lock);

    d->arch.vgic.shared_irqs =
        xzalloc_array(struct vgic_irq_rank, DOMAIN_NR_RANKS(d));
    if ( d->arch.vgic.shared_irqs == NULL )
        return -ENOMEM;

    d->arch.vgic.pending_irqs =
        xzalloc_array(struct pending_irq, d->arch.vgic.nr_spis);
    if ( d->arch.vgic.pending_irqs == NULL )
        return -ENOMEM;

    for (i=0; i<d->arch.vgic.nr_spis; i++)
        vgic_init_pending_irq(&d->arch.vgic.pending_irqs[i], i + 32);

    /* SPIs are routed to VCPU0 by default */
    for ( i = 0; i < DOMAIN_NR_RANKS(d); i++ )
        vgic_rank_init(&d->arch.vgic.shared_irqs[i], i + 1, 0);

    ret = d->arch.vgic.handler->domain_init(d);
    if ( ret )
        return ret;

    d->arch.vgic.allocated_irqs =
        xzalloc_array(unsigned long, BITS_TO_LONGS(vgic_num_irqs(d)));
    if ( !d->arch.vgic.allocated_irqs )
        return -ENOMEM;

    /* vIRQ0-15 (SGIs) are reserved */
    for ( i = 0; i < NR_GIC_SGI; i++ )
        set_bit(i, d->arch.vgic.allocated_irqs);

    return 0;
}

void register_vgic_ops(struct domain *d, const struct vgic_ops *ops)
{
   d->arch.vgic.handler = ops;
}

void domain_vgic_free(struct domain *d)
{
    int i;
    int ret;

    for ( i = 0; i < (d->arch.vgic.nr_spis); i++ )
    {
        struct pending_irq *p = spi_to_pending(d, i + 32);

        if ( p->desc )
        {
            ret = release_guest_irq(d, p->irq);
            if ( ret )
                dprintk(XENLOG_G_WARNING, "d%u: Failed to release virq %u ret = %d\n",
                        d->domain_id, p->irq, ret);
        }
    }

    if ( d->arch.vgic.handler )
        d->arch.vgic.handler->domain_free(d);
    xfree(d->arch.vgic.shared_irqs);
    xfree(d->arch.vgic.pending_irqs);
    xfree(d->arch.vgic.allocated_irqs);
}

int vcpu_vgic_init(struct vcpu *v)
{
    int i;

    v->arch.vgic.private_irqs = xzalloc(struct vgic_irq_rank);
    if ( v->arch.vgic.private_irqs == NULL )
      return -ENOMEM;

    /* SGIs/PPIs are always routed to this VCPU */
    vgic_rank_init(v->arch.vgic.private_irqs, 0, v->vcpu_id);

    v->domain->arch.vgic.handler->vcpu_init(v);

    memset(&v->arch.vgic.pending_irqs, 0, sizeof(v->arch.vgic.pending_irqs));
    for (i = 0; i < 32; i++)
        vgic_init_pending_irq(&v->arch.vgic.pending_irqs[i], i);

    INIT_LIST_HEAD(&v->arch.vgic.inflight_irqs);
    INIT_LIST_HEAD(&v->arch.vgic.lr_pending);
    spin_lock_init(&v->arch.vgic.lock);

    return 0;
}

int vcpu_vgic_free(struct vcpu *v)
{
    xfree(v->arch.vgic.private_irqs);
    return 0;
}

struct vcpu *vgic_get_target_vcpu(struct vcpu *v, unsigned int virq)
{
    struct vgic_irq_rank *rank = vgic_rank_irq(v, virq);
    int target = read_atomic(&rank->vcpu[virq & INTERRUPT_RANK_MASK]);
    return v->domain->vcpu[target];
}

static int vgic_get_virq_priority(struct vcpu *v, unsigned int virq)
{
    struct vgic_irq_rank *rank;

    /* LPIs don't have a rank, also store their priority separately. */
    if ( is_lpi(virq) )
        return v->domain->arch.vgic.handler->lpi_get_priority(v->domain, virq);

    rank = vgic_rank_irq(v, virq);
    return ACCESS_ONCE(rank->priority[virq & INTERRUPT_RANK_MASK]);
}

bool vgic_migrate_irq(struct vcpu *old, struct vcpu *new, unsigned int irq)
{
    unsigned long flags;
    struct pending_irq *p;

    /* This will never be called for an LPI, as we don't migrate them. */
    ASSERT(!is_lpi(irq));

    spin_lock_irqsave(&old->arch.vgic.lock, flags);

    p = irq_to_pending(old, irq);

    /* nothing to do for virtual interrupts */
    if ( p->desc == NULL )
    {
        spin_unlock_irqrestore(&old->arch.vgic.lock, flags);
        return true;
    }

    /* migration already in progress, no need to do anything */
    if ( test_bit(GIC_IRQ_GUEST_MIGRATING, &p->status) )
    {
        gprintk(XENLOG_WARNING, "irq %u migration failed: requested while in progress\n", irq);
        spin_unlock_irqrestore(&old->arch.vgic.lock, flags);
        return false;
    }

    perfc_incr(vgic_irq_migrates);

    if ( list_empty(&p->inflight) )
    {
        irq_set_affinity(p->desc, cpumask_of(new->processor));
        spin_unlock_irqrestore(&old->arch.vgic.lock, flags);
        return true;
    }
    /* If the IRQ is still lr_pending, re-inject it to the new vcpu */
    if ( !list_empty(&p->lr_queue) )
    {
        vgic_remove_irq_from_queues(old, p);
        irq_set_affinity(p->desc, cpumask_of(new->processor));
        spin_unlock_irqrestore(&old->arch.vgic.lock, flags);
        vgic_inject_irq(new->domain, new, irq, true);
        return true;
    }
    /* if the IRQ is in a GICH_LR register, set GIC_IRQ_GUEST_MIGRATING
     * and wait for the EOI */
    if ( !list_empty(&p->inflight) )
        set_bit(GIC_IRQ_GUEST_MIGRATING, &p->status);

    spin_unlock_irqrestore(&old->arch.vgic.lock, flags);
    return true;
}

void arch_move_irqs(struct vcpu *v)
{
    const cpumask_t *cpu_mask = cpumask_of(v->processor);
    struct domain *d = v->domain;
    struct pending_irq *p;
    struct vcpu *v_target;
    int i;

    /*
     * We don't migrate LPIs at the moment.
     * If we ever do, we must make sure that the struct pending_irq does
     * not go away, as there is no lock preventing this here.
     * To ensure this, we check if the loop below ever touches LPIs.
     * In the moment vgic_num_irqs() just covers SPIs, as it's mostly used
     * for allocating the pending_irq and irq_desc array, in which LPIs
     * don't participate.
     */
    ASSERT(!is_lpi(vgic_num_irqs(d) - 1));

    for ( i = 32; i < vgic_num_irqs(d); i++ )
    {
        v_target = vgic_get_target_vcpu(v, i);
        p = irq_to_pending(v_target, i);

        if ( v_target == v && !test_bit(GIC_IRQ_GUEST_MIGRATING, &p->status) )
            irq_set_affinity(p->desc, cpu_mask);
    }
}

void vgic_disable_irqs(struct vcpu *v, uint32_t r, unsigned int n)
{
    const unsigned long mask = r;
    struct pending_irq *p;
    struct irq_desc *desc;
    unsigned int irq;
    unsigned long flags;
    unsigned int i = 0;
    struct vcpu *v_target;

    /* LPIs will never be disabled via this function. */
    ASSERT(!is_lpi(32 * n + 31));

    while ( (i = find_next_bit(&mask, 32, i)) < 32 ) {
        irq = i + (32 * n);
        v_target = vgic_get_target_vcpu(v, irq);

        spin_lock_irqsave(&v_target->arch.vgic.lock, flags);
        p = irq_to_pending(v_target, irq);
        clear_bit(GIC_IRQ_GUEST_ENABLED, &p->status);
        gic_remove_from_lr_pending(v_target, p);
        desc = p->desc;
        spin_unlock_irqrestore(&v_target->arch.vgic.lock, flags);

        if ( desc != NULL )
        {
            spin_lock_irqsave(&desc->lock, flags);
            desc->handler->disable(desc);
            spin_unlock_irqrestore(&desc->lock, flags);
        }
        i++;
    }
}

#define VGIC_ICFG_MASK(intr) (1U << ((2 * ((intr) % 16)) + 1))

/* The function should be called with the rank lock taken */
static inline unsigned int vgic_get_virq_type(struct vcpu *v,
                                              unsigned int n,
                                              unsigned int index)
{
    struct vgic_irq_rank *r = vgic_get_rank(v, n);
    uint32_t tr = r->icfg[index >> 4];

    ASSERT(spin_is_locked(&r->lock));

    if ( tr & VGIC_ICFG_MASK(index) )
        return IRQ_TYPE_EDGE_RISING;
    else
        return IRQ_TYPE_LEVEL_HIGH;
}

void vgic_enable_irqs(struct vcpu *v, uint32_t r, unsigned int n)
{
    const unsigned long mask = r;
    struct pending_irq *p;
    unsigned int irq;
    unsigned long flags;
    unsigned int i = 0;
    struct vcpu *v_target;
    struct domain *d = v->domain;

    /* LPIs will never be enabled via this function. */
    ASSERT(!is_lpi(32 * n + 31));

    while ( (i = find_next_bit(&mask, 32, i)) < 32 ) {
        irq = i + (32 * n);
        v_target = vgic_get_target_vcpu(v, irq);
        spin_lock_irqsave(&v_target->arch.vgic.lock, flags);
        p = irq_to_pending(v_target, irq);
        set_bit(GIC_IRQ_GUEST_ENABLED, &p->status);
        if ( !list_empty(&p->inflight) && !test_bit(GIC_IRQ_GUEST_VISIBLE, &p->status) )
            gic_raise_guest_irq(v_target, irq, p->priority);
        spin_unlock_irqrestore(&v_target->arch.vgic.lock, flags);
        if ( p->desc != NULL )
        {
            irq_set_affinity(p->desc, cpumask_of(v_target->processor));
            spin_lock_irqsave(&p->desc->lock, flags);
            /*
             * The irq cannot be a PPI, we only support delivery of SPIs
             * to guests.
             */
            ASSERT(irq >= 32);
            if ( irq_type_set_by_domain(d) )
                gic_set_irq_type(p->desc, vgic_get_virq_type(v, n, i));
            p->desc->handler->enable(p->desc);
            spin_unlock_irqrestore(&p->desc->lock, flags);
        }
        i++;
    }
}

void vgic_set_irqs_pending(struct vcpu *v, uint32_t r, unsigned int rank)
{
    const unsigned long mask = r;
    unsigned int i;
    /* The first rank is always per-vCPU */
    bool private = rank == 0;

    /* LPIs will never be set pending via this function */
    ASSERT(!is_lpi(32 * rank + 31));

    for_each_set_bit( i, &mask, 32 )
    {
        unsigned int irq = i + 32 * rank;

        if ( !private )
        {
            struct pending_irq *p = spi_to_pending(v->domain, irq);

            /*
             * When the domain sets the pending state for a HW interrupt on
             * the virtual distributor, we set the pending state on the
             * physical distributor.
             *
             * XXX: Investigate whether we would be able to set the
             * physical interrupt active and save an interruption. (This
             * is what the new vGIC does).
             */
            if ( p->desc != NULL )
            {
                unsigned long flags;

                spin_lock_irqsave(&p->desc->lock, flags);
                gic_set_pending_state(p->desc, true);
                spin_unlock_irqrestore(&p->desc->lock, flags);
                continue;
            }
        }

        /*
         * If the interrupt is per-vCPU, then we want to inject the vIRQ
         * to v, otherwise we should let the function figuring out the
         * correct vCPU.
         */
        vgic_inject_irq(v->domain, private ? v : NULL, irq, true);
    }
}

bool vgic_to_sgi(struct vcpu *v, register_t sgir, enum gic_sgi_mode irqmode,
                 int virq, const struct sgi_target *target)
{
    struct domain *d = v->domain;
    int vcpuid;
    int i;
    unsigned int base;
    unsigned long int bitmap;

    ASSERT( virq < 16 );

    switch ( irqmode )
    {
    case SGI_TARGET_LIST:
        perfc_incr(vgic_sgi_list);
        base = target->aff1 << 4;
        bitmap = target->list;
        for_each_set_bit( i, &bitmap, sizeof(target->list) * 8 )
        {
            vcpuid = base + i;
            if ( vcpuid >= d->max_vcpus || d->vcpu[vcpuid] == NULL ||
                 !is_vcpu_online(d->vcpu[vcpuid]) )
            {
                gprintk(XENLOG_WARNING, "VGIC: write r=%"PRIregister" \
                        target->list=%hx, wrong CPUTargetList \n",
                        sgir, target->list);
                continue;
            }
            vgic_inject_irq(d, d->vcpu[vcpuid], virq, true);
        }
        break;
    case SGI_TARGET_OTHERS:
        perfc_incr(vgic_sgi_others);
        for ( i = 0; i < d->max_vcpus; i++ )
        {
            if ( i != current->vcpu_id && d->vcpu[i] != NULL &&
                 is_vcpu_online(d->vcpu[i]) )
                vgic_inject_irq(d, d->vcpu[i], virq, true);
        }
        break;
    case SGI_TARGET_SELF:
        perfc_incr(vgic_sgi_self);
        vgic_inject_irq(d, current, virq, true);
        break;
    default:
        gprintk(XENLOG_WARNING,
                "vGICD:unhandled GICD_SGIR write %"PRIregister" \
                 with wrong mode\n", sgir);
        return false;
    }

    return true;
}

/*
 * Returns the pointer to the struct pending_irq belonging to the given
 * interrupt.
 * This can return NULL if called for an LPI which has been unmapped
 * meanwhile.
 */
struct pending_irq *irq_to_pending(struct vcpu *v, unsigned int irq)
{
    struct pending_irq *n;
    /* Pending irqs allocation strategy: the first vgic.nr_spis irqs
     * are used for SPIs; the rests are used for per cpu irqs */
    if ( irq < 32 )
        n = &v->arch.vgic.pending_irqs[irq];
    else if ( is_lpi(irq) )
        n = v->domain->arch.vgic.handler->lpi_to_pending(v->domain, irq);
    else
        n = &v->domain->arch.vgic.pending_irqs[irq - 32];
    return n;
}

struct pending_irq *spi_to_pending(struct domain *d, unsigned int irq)
{
    ASSERT(irq >= NR_LOCAL_IRQS);

    return &d->arch.vgic.pending_irqs[irq - 32];
}

void vgic_clear_pending_irqs(struct vcpu *v)
{
    struct pending_irq *p, *t;
    unsigned long flags;

    spin_lock_irqsave(&v->arch.vgic.lock, flags);
    list_for_each_entry_safe ( p, t, &v->arch.vgic.inflight_irqs, inflight )
        list_del_init(&p->inflight);
    gic_clear_pending_irqs(v);
    spin_unlock_irqrestore(&v->arch.vgic.lock, flags);
}

void vgic_remove_irq_from_queues(struct vcpu *v, struct pending_irq *p)
{
    ASSERT(spin_is_locked(&v->arch.vgic.lock));

    clear_bit(GIC_IRQ_GUEST_QUEUED, &p->status);
    list_del_init(&p->inflight);
    gic_remove_from_lr_pending(v, p);
}

void vgic_inject_irq(struct domain *d, struct vcpu *v, unsigned int virq,
                     bool level)
{
    uint8_t priority;
    struct pending_irq *iter, *n;
    unsigned long flags;

    /*
     * For edge triggered interrupts we always ignore a "falling edge".
     * For level triggered interrupts we shouldn't, but do anyways.
     */
    if ( !level )
        return;

    if ( !v )
    {
        /* The IRQ needs to be an SPI if no vCPU is specified. */
        ASSERT(virq >= 32 && virq <= vgic_num_irqs(d));

        v = vgic_get_target_vcpu(d->vcpu[0], virq);
    };

    spin_lock_irqsave(&v->arch.vgic.lock, flags);

    n = irq_to_pending(v, virq);
    /* If an LPI has been removed, there is nothing to inject here. */
    if ( unlikely(!n) )
    {
        spin_unlock_irqrestore(&v->arch.vgic.lock, flags);
        return;
    }

    /* vcpu offline */
    if ( test_bit(_VPF_down, &v->pause_flags) )
    {
        spin_unlock_irqrestore(&v->arch.vgic.lock, flags);
        return;
    }

    set_bit(GIC_IRQ_GUEST_QUEUED, &n->status);

    if ( !list_empty(&n->inflight) )
    {
        gic_raise_inflight_irq(v, virq);
        goto out;
    }

    priority = vgic_get_virq_priority(v, virq);
    n->priority = priority;

    /* the irq is enabled */
    if ( test_bit(GIC_IRQ_GUEST_ENABLED, &n->status) )
        gic_raise_guest_irq(v, virq, priority);

    list_for_each_entry ( iter, &v->arch.vgic.inflight_irqs, inflight )
    {
        if ( iter->priority > priority )
        {
            list_add_tail(&n->inflight, &iter->inflight);
            goto out;
        }
    }
    list_add_tail(&n->inflight, &v->arch.vgic.inflight_irqs);
out:
    spin_unlock_irqrestore(&v->arch.vgic.lock, flags);

    /* we have a new higher priority irq, inject it into the guest */
    vcpu_kick(v);

    return;
}

bool vgic_evtchn_irq_pending(struct vcpu *v)
{
    struct pending_irq *p;

    p = irq_to_pending(v, v->domain->arch.evtchn_irq);
    /* Does not work for LPIs. */
    ASSERT(!is_lpi(v->domain->arch.evtchn_irq));

    return list_empty(&p->inflight);
}

bool vgic_emulate(struct cpu_user_regs *regs, union hsr hsr)
{
    struct vcpu *v = current;

    ASSERT(v->domain->arch.vgic.handler->emulate_reg != NULL);

    return v->domain->arch.vgic.handler->emulate_reg(regs, hsr);
}

bool vgic_reserve_virq(struct domain *d, unsigned int virq)
{
    if ( virq >= vgic_num_irqs(d) )
        return false;

    return !test_and_set_bit(virq, d->arch.vgic.allocated_irqs);
}

int vgic_allocate_virq(struct domain *d, bool spi)
{
    int first, end;
    unsigned int virq;

    if ( !spi )
    {
        /* We only allocate PPIs. SGIs are all reserved */
        first = 16;
        end = 32;
    }
    else
    {
        first = 32;
        end = vgic_num_irqs(d);
    }

    /*
     * There is no spinlock to protect allocated_irqs, therefore
     * test_and_set_bit may fail. If so retry it.
     */
    do
    {
        virq = find_next_zero_bit(d->arch.vgic.allocated_irqs, end, first);
        if ( virq >= end )
            return -1;
    }
    while ( test_and_set_bit(virq, d->arch.vgic.allocated_irqs) );

    return virq;
}

void vgic_free_virq(struct domain *d, unsigned int virq)
{
    clear_bit(virq, d->arch.vgic.allocated_irqs);
}

unsigned int vgic_max_vcpus(unsigned int domctl_vgic_version)
{
    switch ( domctl_vgic_version )
    {
    case XEN_DOMCTL_CONFIG_GIC_V2:
        return 8;

#ifdef CONFIG_GICV3
    case XEN_DOMCTL_CONFIG_GIC_V3:
        return 4096;
#endif

    default:
        return 0;
    }
}

void vgic_check_inflight_irqs_pending(struct domain *d, struct vcpu *v,
                                      unsigned int rank, uint32_t r)
{
    const unsigned long mask = r;
    unsigned int i;

    for_each_set_bit( i, &mask, 32 )
    {
        struct pending_irq *p;
        struct vcpu *v_target;
        unsigned long flags;
        unsigned int irq = i + 32 * rank;

        v_target = vgic_get_target_vcpu(v, irq);

        spin_lock_irqsave(&v_target->arch.vgic.lock, flags);

        p = irq_to_pending(v_target, irq);

        if ( p && !list_empty(&p->inflight) )
            printk(XENLOG_G_WARNING
                   "%pv trying to clear pending interrupt %u.\n",
                   v, irq);

        spin_unlock_irqrestore(&v_target->arch.vgic.lock, flags);
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


/* === END INLINED: vgic.c === */
/* === BEGIN INLINED: vgic-v3.c === */
#include <xen_xen_config.h>
#include <xen_xen_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * xen/arch/arm/vgic-v3.c
 *
 * ARM Virtual Generic Interrupt Controller v3 support
 * based on xen/arch/arm/vgic.c
 *
 * Vijaya Kumar K <vijaya.kumar@caviumnetworks.com>
 * Copyright (c) 2014 Cavium Inc.
 */

#include <xen_xen_config.h>

#include <xen_bitops.h>
#include <xen_init.h>
#include <xen_irq.h>
#include <xen_lib.h>
#include <xen_sched.h>
#include <xen_softirq.h>
#include <xen_sizes.h>

#include <asm_cpregs.h>
#include <asm_current.h>
#include <asm_gic_v3_defs.h>
#include <asm_gic_v3_its.h>
#include <asm_mmio.h>
#include <asm_vgic.h>
#include <asm_vgic-emul.h>
#include <asm_vreg.h>

/*
 * PIDR2: Only bits[7:4] are not implementation defined. We are
 * emulating a GICv3 ([7:4] = 0x3).
 *
 * We don't emulate a specific registers scheme so implement the others
 * bits as RES0 as recommended by the spec (see 8.1.13 in ARM IHI 0069A).
 */
#define GICV3_GICD_PIDR2  0x30
#define GICV3_GICR_PIDR2  GICV3_GICD_PIDR2

/*
 * GICD_CTLR default value:
 *      - No GICv2 compatibility => ARE = 1
 */
#define VGICD_CTLR_DEFAULT  (GICD_CTLR_ARE_NS)

static struct {
    bool enabled;
    /* Distributor interface address */
    paddr_t dbase;
    /* Re-distributor regions */
    unsigned int nr_rdist_regions;
    const struct rdist_region *regions;
    unsigned int intid_bits;  /* Number of interrupt ID bits */
} vgic_v3_hw;

void vgic_v3_setup_hw(paddr_t dbase,
                      unsigned int nr_rdist_regions,
                      const struct rdist_region *regions,
                      unsigned int intid_bits)
{
    vgic_v3_hw.enabled = true;
    vgic_v3_hw.dbase = dbase;
    vgic_v3_hw.nr_rdist_regions = nr_rdist_regions;
    vgic_v3_hw.regions = regions;
    vgic_v3_hw.intid_bits = intid_bits;
}

static struct vcpu *vgic_v3_irouter_to_vcpu(struct domain *d, uint64_t irouter)
{
    unsigned int vcpu_id;

    /*
     * When the Interrupt Route Mode is set, the IRQ targets any vCPUs.
     * For simplicity, the IRQ is always routed to vCPU0.
     */
    if ( irouter & GICD_IROUTER_SPI_MODE_ANY )
        return d->vcpu[0];

    vcpu_id = vaffinity_to_vcpuid(irouter);
    if ( vcpu_id >= d->max_vcpus )
        return NULL;

    return d->vcpu[vcpu_id];
}

#define NR_BYTES_PER_IROUTER 8U

/*
 * Fetch an IROUTER register based on the offset from IROUTER0. Only one
 * vCPU will be listed for a given vIRQ.
 *
 * Note the byte offset will be aligned to an IROUTER<n> boundary.
 */
static uint64_t vgic_fetch_irouter(struct vgic_irq_rank *rank,
                                   unsigned int offset)
{
    ASSERT(spin_is_locked(&rank->lock));

    /* There is exactly 1 vIRQ per IROUTER */
    offset /= NR_BYTES_PER_IROUTER;

    /* Get the index in the rank */
    offset &= INTERRUPT_RANK_MASK;

    return vcpuid_to_vaffinity(read_atomic(&rank->vcpu[offset]));
}

/*
 * Store an IROUTER register in a convenient way and migrate the vIRQ
 * if necessary. This function only deals with IROUTER32 and onwards.
 *
 * Note the offset will be aligned to the appropriate boundary.
 */
static void vgic_store_irouter(struct domain *d, struct vgic_irq_rank *rank,
                               unsigned int offset, uint64_t irouter)
{
    struct vcpu *new_vcpu, *old_vcpu;
    unsigned int virq;

    /* There is 1 vIRQ per IROUTER */
    virq = offset / NR_BYTES_PER_IROUTER;

    /*
     * The IROUTER0-31, used for SGIs/PPIs, are reserved and should
     * never call this function.
     */
    ASSERT(virq >= 32);

    /* Get the index in the rank */
    offset = virq & INTERRUPT_RANK_MASK;

    new_vcpu = vgic_v3_irouter_to_vcpu(d, irouter);
    old_vcpu = d->vcpu[read_atomic(&rank->vcpu[offset])];

    /*
     * From the spec (see 8.9.13 in IHI 0069A), any write with an
     * invalid vCPU will lead to the interrupt being ignored.
     *
     * But the current code to inject an IRQ is not able to cope with
     * invalid vCPU. So for now, just ignore the write.
     *
     * TODO: Respect the spec
     */
    if ( !new_vcpu )
        return;

    /* Only migrate the IRQ if the target vCPU has changed */
    if ( new_vcpu != old_vcpu )
    {
        if ( vgic_migrate_irq(old_vcpu, new_vcpu, virq) )
            write_atomic(&rank->vcpu[offset], new_vcpu->vcpu_id);
    }
}

static int __vgic_v3_rdistr_rd_mmio_read(struct vcpu *v, mmio_info_t *info,
                                         uint32_t gicr_reg,
                                         register_t *r)
{
    struct hsr_dabt dabt = info->dabt;

    switch ( gicr_reg )
    {
    case VREG32(GICR_CTLR):
    {
        unsigned long flags;

        if ( !v->domain->arch.vgic.has_its )
            goto read_as_zero_32;
        if ( dabt.size != DABT_WORD ) goto bad_width;

        spin_lock_irqsave(&v->arch.vgic.lock, flags);
        *r = vreg_reg32_extract(!!(v->arch.vgic.flags & VGIC_V3_LPIS_ENABLED),
                                info);
        spin_unlock_irqrestore(&v->arch.vgic.lock, flags);
        return 1;
    }

    case VREG32(GICR_IIDR):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        *r = vreg_reg32_extract(GICV3_GICR_IIDR_VAL, info);
        return 1;

    case VREG64(GICR_TYPER):
    {
        uint64_t typer, aff;
        /*
         * This is to enable shifts greater than 32 bits which would have
         * otherwise caused overflow (as v->arch.vmpidr is 32 bit on AArch32).
         */
        uint64_t vmpidr = v->arch.vmpidr;

        if ( !vgic_reg64_check_access(dabt) ) goto bad_width;
        aff = (
#ifdef CONFIG_ARM_64
               MPIDR_AFFINITY_LEVEL(vmpidr, 3) << 56 |
#endif
               MPIDR_AFFINITY_LEVEL(vmpidr, 2) << 48 |
               MPIDR_AFFINITY_LEVEL(vmpidr, 1) << 40 |
               MPIDR_AFFINITY_LEVEL(vmpidr, 0) << 32);
        typer = aff;
        /* We use the VCPU ID as the redistributor ID in bits[23:8] */
        typer |= v->vcpu_id << GICR_TYPER_PROC_NUM_SHIFT;

        if ( v->arch.vgic.flags & VGIC_V3_RDIST_LAST )
            typer |= GICR_TYPER_LAST;

        if ( v->domain->arch.vgic.has_its )
            typer |= GICR_TYPER_PLPIS;

        *r = vreg_reg64_extract(typer, info);

        return 1;
    }

    case VREG32(GICR_STATUSR):
        /* Not implemented */
        goto read_as_zero_32;

    case VREG32(GICR_WAKER):
        /* Power management is not implemented */
        goto read_as_zero_32;

    case 0x0018:
        goto read_reserved;

    case 0x0020:
        goto read_impl_defined;

    case VREG64(GICR_SETLPIR):
        /* WO. Read unknown */
        goto read_unknown;

    case VREG64(GICR_CLRLPIR):
        /* WO. Read unknown */
        goto read_unknown;

    case 0x0050:
        goto read_reserved;

    case VREG64(GICR_PROPBASER):
        if ( !v->domain->arch.vgic.has_its )
            goto read_as_zero_64;
        if ( !vgic_reg64_check_access(dabt) ) goto bad_width;

        vgic_lock(v);
        *r = vreg_reg64_extract(v->domain->arch.vgic.rdist_propbase, info);
        vgic_unlock(v);
        return 1;

    case VREG64(GICR_PENDBASER):
    {
        uint64_t val;

        if ( !v->domain->arch.vgic.has_its )
            goto read_as_zero_64;
        if ( !vgic_reg64_check_access(dabt) ) goto bad_width;

        val = read_atomic(&v->arch.vgic.rdist_pendbase);
        val &= ~GICR_PENDBASER_PTZ;      /* WO, reads as 0 */
        *r = vreg_reg64_extract(val, info);
        return 1;
    }

    case 0x0080:
        goto read_reserved;

    case VREG64(GICR_INVLPIR):
        /* WO. Read unknown */
        goto read_unknown;

    case 0x00A8:
        goto read_reserved;

    case VREG64(GICR_INVALLR):
        /* WO. Read unknown */
        goto read_unknown;

    case 0x00B8:
        goto read_reserved;

    case VREG32(GICR_SYNCR):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        /* RO . But when read it always returns busy bito bit[0] */
        *r = vreg_reg32_extract(GICR_SYNCR_NOT_BUSY, info);
        return 1;

    case 0x00C8:
        goto read_reserved;

    case VREG64(0x0100):
        goto read_impl_defined;

    case 0x0108:
        goto read_reserved;

    case VREG64(0x0110):
        goto read_impl_defined;

    case 0x0118 ... 0xBFFC:
        goto read_reserved;

    case 0xC000 ... 0xFFCC:
        goto read_impl_defined;

    case 0xFFD0 ... 0xFFE4:
        /* Implementation defined identification registers */
       goto read_impl_defined;

    case VREG32(GICR_PIDR2):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        *r = vreg_reg32_extract(GICV3_GICR_PIDR2, info);
         return 1;

    case 0xFFEC ... 0xFFFC:
         /* Implementation defined identification registers */
         goto read_impl_defined;

    default:
        printk(XENLOG_G_ERR
               "%pv: vGICR: unhandled read r%d offset %#08x\n",
               v, dabt.reg, gicr_reg);
        goto read_as_zero;
    }
bad_width:
    printk(XENLOG_G_ERR "%pv vGICR: bad read width %d r%d offset %#08x\n",
           v, dabt.size, dabt.reg, gicr_reg);
    return 0;

read_as_zero_64:
    if ( !vgic_reg64_check_access(dabt) ) goto bad_width;
    *r = 0;
    return 1;

read_as_zero_32:
    if ( dabt.size != DABT_WORD ) goto bad_width;
    *r = 0;
    return 1;

read_as_zero:
    *r = 0;
    return 1;

read_impl_defined:
    printk(XENLOG_G_DEBUG
           "%pv: vGICR: RAZ on implementation defined register offset %#08x\n",
           v, gicr_reg);
    *r = 0;
    return 1;

read_reserved:
    printk(XENLOG_G_DEBUG
           "%pv: vGICR: RAZ on reserved register offset %#08x\n",
           v, gicr_reg);
    *r = 0;
    return 1;

read_unknown:
    *r = vreg_reg64_extract(0xdeadbeafdeadbeafULL, info);
    return 1;
}

static uint64_t vgic_sanitise_field(uint64_t reg, uint64_t field_mask,
                                    int field_shift,
                                    uint64_t (*sanitise_fn)(uint64_t field))
{
    uint64_t field = (reg & field_mask) >> field_shift;

    field = sanitise_fn(field) << field_shift;

    return (reg & ~field_mask) | field;
}

/* We want to avoid outer shareable. */
static uint64_t vgic_sanitise_shareability(uint64_t field)
{
    switch ( field )
    {
    case GIC_BASER_OuterShareable:
        return GIC_BASER_InnerShareable;
    default:
        return field;
    }
}

/* Avoid any inner non-cacheable mapping. */
static uint64_t vgic_sanitise_inner_cacheability(uint64_t field)
{
    switch ( field )
    {
    case GIC_BASER_CACHE_nCnB:
    case GIC_BASER_CACHE_nC:
        return GIC_BASER_CACHE_RaWb;
    default:
        return field;
    }
}

/* Non-cacheable or same-as-inner are OK. */
static uint64_t vgic_sanitise_outer_cacheability(uint64_t field)
{
    switch ( field )
    {
    case GIC_BASER_CACHE_SameAsInner:
    case GIC_BASER_CACHE_nC:
        return field;
    default:
        return GIC_BASER_CACHE_nC;
    }
}

static uint64_t sanitize_propbaser(uint64_t reg)
{
    reg = vgic_sanitise_field(reg, GICR_PROPBASER_SHAREABILITY_MASK,
                              GICR_PROPBASER_SHAREABILITY_SHIFT,
                              vgic_sanitise_shareability);
    reg = vgic_sanitise_field(reg, GICR_PROPBASER_INNER_CACHEABILITY_MASK,
                              GICR_PROPBASER_INNER_CACHEABILITY_SHIFT,
                              vgic_sanitise_inner_cacheability);
    reg = vgic_sanitise_field(reg, GICR_PROPBASER_OUTER_CACHEABILITY_MASK,
                              GICR_PROPBASER_OUTER_CACHEABILITY_SHIFT,
                              vgic_sanitise_outer_cacheability);

    reg &= ~GICR_PROPBASER_RES0_MASK;

    return reg;
}

static uint64_t sanitize_pendbaser(uint64_t reg)
{
    reg = vgic_sanitise_field(reg, GICR_PENDBASER_SHAREABILITY_MASK,
                              GICR_PENDBASER_SHAREABILITY_SHIFT,
                              vgic_sanitise_shareability);
    reg = vgic_sanitise_field(reg, GICR_PENDBASER_INNER_CACHEABILITY_MASK,
                              GICR_PENDBASER_INNER_CACHEABILITY_SHIFT,
                              vgic_sanitise_inner_cacheability);
    reg = vgic_sanitise_field(reg, GICR_PENDBASER_OUTER_CACHEABILITY_MASK,
                              GICR_PENDBASER_OUTER_CACHEABILITY_SHIFT,
                              vgic_sanitise_outer_cacheability);

    reg &= ~GICR_PENDBASER_RES0_MASK;

    return reg;
}

static void vgic_vcpu_enable_lpis(struct vcpu *v)
{
    uint64_t reg = v->domain->arch.vgic.rdist_propbase;
    unsigned int nr_lpis = BIT((reg & 0x1f) + 1, UL);

    /* rdists_enabled is protected by the domain lock. */
    ASSERT(spin_is_locked(&v->domain->arch.vgic.lock));

    if ( nr_lpis < LPI_OFFSET )
        nr_lpis = 0;
    else
        nr_lpis -= LPI_OFFSET;

    if ( !v->domain->arch.vgic.rdists_enabled )
    {
        v->domain->arch.vgic.nr_lpis = nr_lpis;
        /*
         * Make sure nr_lpis is visible before rdists_enabled.
         * We read nr_lpis (and rdist_propbase) outside of the lock in
         * other functions, but guard those accesses by rdists_enabled, so
         * make sure these are consistent.
         */
        smp_mb();
        v->domain->arch.vgic.rdists_enabled = true;
        /*
         * Make sure the per-domain rdists_enabled flag has been set before
         * enabling this particular redistributor.
         */
        smp_mb();
    }

    v->arch.vgic.flags |= VGIC_V3_LPIS_ENABLED;
}

static int __vgic_v3_rdistr_rd_mmio_write(struct vcpu *v, mmio_info_t *info,
                                          uint32_t gicr_reg,
                                          register_t r)
{
    struct hsr_dabt dabt = info->dabt;
    uint64_t reg;

    switch ( gicr_reg )
    {
    case VREG32(GICR_CTLR):
    {
        unsigned long flags;

        if ( !v->domain->arch.vgic.has_its )
            goto write_ignore_32;
        if ( dabt.size != DABT_WORD ) goto bad_width;

        vgic_lock(v);                   /* protects rdists_enabled */
        spin_lock_irqsave(&v->arch.vgic.lock, flags);

        /* LPIs can only be enabled once, but never disabled again. */
        if ( (r & GICR_CTLR_ENABLE_LPIS) &&
             !(v->arch.vgic.flags & VGIC_V3_LPIS_ENABLED) )
            vgic_vcpu_enable_lpis(v);

        spin_unlock_irqrestore(&v->arch.vgic.lock, flags);
        vgic_unlock(v);

        return 1;
    }

    case VREG32(GICR_IIDR):
        /* RO */
        goto write_ignore_32;

    case VREG64(GICR_TYPER):
        /* RO */
        goto write_ignore_64;

    case VREG32(GICR_STATUSR):
        /* Not implemented */
        goto write_ignore_32;

    case VREG32(GICR_WAKER):
        /* Power mgmt not implemented */
        goto write_ignore_32;

    case 0x0018:
        goto write_reserved;

    case 0x0020:
        goto write_impl_defined;

    case VREG64(GICR_SETLPIR):
        /* LPIs without an ITS are not implemented */
        goto write_ignore_64;

    case VREG64(GICR_CLRLPIR):
        /* LPIs without an ITS are not implemented */
        goto write_ignore_64;

    case 0x0050:
        goto write_reserved;

    case VREG64(GICR_PROPBASER):
        if ( !v->domain->arch.vgic.has_its )
            goto write_ignore_64;
        if ( !vgic_reg64_check_access(dabt) ) goto bad_width;

        vgic_lock(v);

        /*
         * Writing PROPBASER with any redistributor having LPIs enabled
         * is UNPREDICTABLE.
         */
        if ( !(v->domain->arch.vgic.rdists_enabled) )
        {
            reg = v->domain->arch.vgic.rdist_propbase;
            vreg_reg64_update(&reg, r, info);
            reg = sanitize_propbaser(reg);
            v->domain->arch.vgic.rdist_propbase = reg;
        }

        vgic_unlock(v);

        return 1;

    case VREG64(GICR_PENDBASER):
    {
        unsigned long flags;

        if ( !v->domain->arch.vgic.has_its )
            goto write_ignore_64;
        if ( !vgic_reg64_check_access(dabt) ) goto bad_width;

        spin_lock_irqsave(&v->arch.vgic.lock, flags);

        /* Writing PENDBASER with LPIs enabled is UNPREDICTABLE. */
        if ( !(v->arch.vgic.flags & VGIC_V3_LPIS_ENABLED) )
        {
            reg = read_atomic(&v->arch.vgic.rdist_pendbase);
            vreg_reg64_update(&reg, r, info);
            reg = sanitize_pendbaser(reg);
            write_atomic(&v->arch.vgic.rdist_pendbase, reg);
        }

        spin_unlock_irqrestore(&v->arch.vgic.lock, flags);

        return 1;
    }

    case 0x0080:
        goto write_reserved;

    case VREG64(GICR_INVLPIR):
        /* LPIs without an ITS are not implemented */
        goto write_ignore_64;

    case 0x00A8:
        goto write_reserved;

    case VREG64(GICR_INVALLR):
        /* LPIs without an ITS are not implemented */
        goto write_ignore_64;

    case 0x00B8:
        goto write_reserved;

    case VREG32(GICR_SYNCR):
        /* RO */
        goto write_ignore_32;

    case 0x00C8:
        goto write_reserved;

    case VREG64(0x0100):
        goto write_impl_defined;

    case 0x0108:
        goto write_reserved;

    case VREG64(0x0110):
        goto write_impl_defined;

    case 0x0118 ... 0xBFFC:
        goto write_reserved;

    case 0xC000 ... 0xFFCC:
        goto write_impl_defined;

    case 0xFFD0 ... 0xFFE4:
        /* Implementation defined identification registers */
       goto write_impl_defined;

    case VREG32(GICR_PIDR2):
        /* RO */
        goto write_ignore_32;

    case 0xFFEC ... 0xFFFC:
         /* Implementation defined identification registers */
         goto write_impl_defined;

    default:
        printk(XENLOG_G_ERR "%pv: vGICR: unhandled write r%d offset %#08x\n",
               v, dabt.reg, gicr_reg);
        goto write_ignore;
    }
bad_width:
    printk(XENLOG_G_ERR
          "%pv: vGICR: bad write width %d r%d=%"PRIregister" offset %#08x\n",
          v, dabt.size, dabt.reg, r, gicr_reg);
    return 0;

write_ignore_64:
    if ( vgic_reg64_check_access(dabt) ) goto bad_width;
    return 1;

write_ignore_32:
    if ( dabt.size != DABT_WORD ) goto bad_width;
    return 1;

write_ignore:
    return 1;

write_impl_defined:
    printk(XENLOG_G_DEBUG
           "%pv: vGICR: WI on implementation defined register offset %#08x\n",
           v, gicr_reg);
    return 1;

write_reserved:
    printk(XENLOG_G_DEBUG
           "%pv: vGICR: WI on reserved register offset %#08x\n",
           v, gicr_reg);
    return 1;
}

static int __vgic_v3_distr_common_mmio_read(const char *name, struct vcpu *v,
                                            mmio_info_t *info, uint32_t reg,
                                            register_t *r)
{
    struct hsr_dabt dabt = info->dabt;
    struct vgic_irq_rank *rank;
    unsigned long flags;

    switch ( reg )
    {
    case VRANGE32(GICD_IGROUPR, GICD_IGROUPRN):
    case VRANGE32(GICD_IGRPMODR, GICD_IGRPMODRN):
        /* We do not implement security extensions for guests, read zero */
        if ( dabt.size != DABT_WORD ) goto bad_width;
        goto read_as_zero;

    case VRANGE32(GICD_ISENABLER, GICD_ISENABLERN):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        rank = vgic_rank_offset(v, 1, reg - GICD_ISENABLER, DABT_WORD);
        if ( rank == NULL ) goto read_as_zero;
        vgic_lock_rank(v, rank, flags);
        *r = vreg_reg32_extract(rank->ienable, info);
        vgic_unlock_rank(v, rank, flags);
        return 1;

    case VRANGE32(GICD_ICENABLER, GICD_ICENABLERN):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        rank = vgic_rank_offset(v, 1, reg - GICD_ICENABLER, DABT_WORD);
        if ( rank == NULL ) goto read_as_zero;
        vgic_lock_rank(v, rank, flags);
        *r = vreg_reg32_extract(rank->ienable, info);
        vgic_unlock_rank(v, rank, flags);
        return 1;

    /* Read the pending status of an IRQ via GICD/GICR is not supported */
    case VRANGE32(GICD_ISPENDR, GICD_ISPENDRN):
    case VRANGE32(GICD_ICPENDR, GICD_ICPENDR):
        goto read_as_zero;

    /* Read the active status of an IRQ via GICD/GICR is not supported */
    case VRANGE32(GICD_ISACTIVER, GICD_ISACTIVERN):
    case VRANGE32(GICD_ICACTIVER, GICD_ICACTIVERN):
        goto read_as_zero;

    case VRANGE32(GICD_IPRIORITYR, GICD_IPRIORITYRN):
    {
        uint32_t ipriorityr;
        uint8_t rank_index;

        if ( dabt.size != DABT_BYTE && dabt.size != DABT_WORD ) goto bad_width;
        rank = vgic_rank_offset(v, 8, reg - GICD_IPRIORITYR, DABT_WORD);
        if ( rank == NULL ) goto read_as_zero;
        rank_index = REG_RANK_INDEX(8, reg - GICD_IPRIORITYR, DABT_WORD);

        vgic_lock_rank(v, rank, flags);
        ipriorityr = ACCESS_ONCE(rank->ipriorityr[rank_index]);
        vgic_unlock_rank(v, rank, flags);

        *r = vreg_reg32_extract(ipriorityr, info);

        return 1;
    }

    case VRANGE32(GICD_ICFGR, GICD_ICFGRN):
    {
        uint32_t icfgr;

        if ( dabt.size != DABT_WORD ) goto bad_width;
        rank = vgic_rank_offset(v, 2, reg - GICD_ICFGR, DABT_WORD);
        if ( rank == NULL ) goto read_as_zero;
        vgic_lock_rank(v, rank, flags);
        icfgr = rank->icfg[REG_RANK_INDEX(2, reg - GICD_ICFGR, DABT_WORD)];
        vgic_unlock_rank(v, rank, flags);

        *r = vreg_reg32_extract(icfgr, info);

        return 1;
    }

    default:
        printk(XENLOG_G_ERR
               "%pv: %s: unhandled read r%d offset %#08x\n",
               v, name, dabt.reg, reg);
        return 0;
    }

bad_width:
    printk(XENLOG_G_ERR "%pv: %s: bad read width %d r%d offset %#08x\n",
           v, name, dabt.size, dabt.reg, reg);
    return 0;

read_as_zero:
    *r = 0;
    return 1;
}

static int __vgic_v3_distr_common_mmio_write(const char *name, struct vcpu *v,
                                             mmio_info_t *info, uint32_t reg,
                                             register_t r)
{
    struct hsr_dabt dabt = info->dabt;
    struct vgic_irq_rank *rank;
    uint32_t tr;
    unsigned long flags;

    switch ( reg )
    {
    case VRANGE32(GICD_IGROUPR, GICD_IGROUPRN):
    case VRANGE32(GICD_IGRPMODR, GICD_IGRPMODRN):
        /* We do not implement security extensions for guests, write ignore */
        goto write_ignore_32;

    case VRANGE32(GICD_ISENABLER, GICD_ISENABLERN):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        rank = vgic_rank_offset(v, 1, reg - GICD_ISENABLER, DABT_WORD);
        if ( rank == NULL ) goto write_ignore;
        vgic_lock_rank(v, rank, flags);
        tr = rank->ienable;
        vreg_reg32_setbits(&rank->ienable, r, info);
        vgic_enable_irqs(v, (rank->ienable) & (~tr), rank->index);
        vgic_unlock_rank(v, rank, flags);
        return 1;

    case VRANGE32(GICD_ICENABLER, GICD_ICENABLERN):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        rank = vgic_rank_offset(v, 1, reg - GICD_ICENABLER, DABT_WORD);
        if ( rank == NULL ) goto write_ignore;
        vgic_lock_rank(v, rank, flags);
        tr = rank->ienable;
        vreg_reg32_clearbits(&rank->ienable, r, info);
        vgic_disable_irqs(v, (~rank->ienable) & tr, rank->index);
        vgic_unlock_rank(v, rank, flags);
        return 1;

    case VRANGE32(GICD_ISPENDR, GICD_ISPENDRN):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        rank = vgic_rank_offset(v, 1, reg - GICD_ISPENDR, DABT_WORD);
        if ( rank == NULL ) goto write_ignore;

        vgic_set_irqs_pending(v, r, rank->index);

        return 1;

    case VRANGE32(GICD_ICPENDR, GICD_ICPENDRN):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        rank = vgic_rank_offset(v, 1, reg - GICD_ICPENDR, DABT_WORD);
        if ( rank == NULL ) goto write_ignore;

        vgic_check_inflight_irqs_pending(v->domain, v, rank->index, r);

        goto write_ignore;

    case VRANGE32(GICD_ISACTIVER, GICD_ISACTIVERN):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        printk(XENLOG_G_ERR
               "%pv: %s: unhandled word write %#"PRIregister" to ISACTIVER%d\n",
               v, name, r, reg - GICD_ISACTIVER);
        return 0;

    case VRANGE32(GICD_ICACTIVER, GICD_ICACTIVERN):
        printk(XENLOG_G_ERR
               "%pv: %s: unhandled word write %#"PRIregister" to ICACTIVER%d\n",
               v, name, r, reg - GICD_ICACTIVER);
        goto write_ignore_32;

    case VRANGE32(GICD_IPRIORITYR, GICD_IPRIORITYRN):
    {
        uint32_t *ipriorityr, priority;

        if ( dabt.size != DABT_BYTE && dabt.size != DABT_WORD ) goto bad_width;
        rank = vgic_rank_offset(v, 8, reg - GICD_IPRIORITYR, DABT_WORD);
        if ( rank == NULL ) goto write_ignore;
        vgic_lock_rank(v, rank, flags);
        ipriorityr = &rank->ipriorityr[REG_RANK_INDEX(8, reg - GICD_IPRIORITYR,
                                                      DABT_WORD)];
        priority = ACCESS_ONCE(*ipriorityr);
        vreg_reg32_update(&priority, r, info);
        ACCESS_ONCE(*ipriorityr) = priority;
        vgic_unlock_rank(v, rank, flags);
        return 1;
    }

    case VREG32(GICD_ICFGR): /* Restricted to configure SGIs */
        goto write_ignore_32;

    case VRANGE32(GICD_ICFGR + 4, GICD_ICFGRN): /* PPI + SPIs */
        /* ICFGR1 for PPI's, which is implementation defined
           if ICFGR1 is programmable or not. We chose to program */
        if ( dabt.size != DABT_WORD ) goto bad_width;
        rank = vgic_rank_offset(v, 2, reg - GICD_ICFGR, DABT_WORD);
        if ( rank == NULL ) goto write_ignore;
        vgic_lock_rank(v, rank, flags);
        vreg_reg32_update(&rank->icfg[REG_RANK_INDEX(2, reg - GICD_ICFGR,
                                                     DABT_WORD)],
                          r, info);
        vgic_unlock_rank(v, rank, flags);
        return 1;

    default:
        printk(XENLOG_G_ERR
               "%pv: %s: unhandled write r%d=%"PRIregister" offset %#08x\n",
               v, name, dabt.reg, r, reg);
        return 0;
    }

bad_width:
    printk(XENLOG_G_ERR
           "%pv: %s: bad write width %d r%d=%"PRIregister" offset %#08x\n",
           v, name, dabt.size, dabt.reg, r, reg);
    return 0;

write_ignore_32:
    if ( dabt.size != DABT_WORD ) goto bad_width;
write_ignore:
    return 1;
}

static int vgic_v3_rdistr_sgi_mmio_read(struct vcpu *v, mmio_info_t *info,
                                        uint32_t gicr_reg, register_t *r)
{
    struct hsr_dabt dabt = info->dabt;

    switch ( gicr_reg )
    {
    case VREG32(GICR_IGROUPR0):
    case VREG32(GICR_ISENABLER0):
    case VREG32(GICR_ICENABLER0):
    case VREG32(GICR_ISACTIVER0):
    case VREG32(GICR_ICACTIVER0):
    case VRANGE32(GICR_IPRIORITYR0, GICR_IPRIORITYR7):
    case VRANGE32(GICR_ICFGR0, GICR_ICFGR1):
         /*
          * Above registers offset are common with GICD.
          * So handle in common with GICD handling
          */
        return __vgic_v3_distr_common_mmio_read("vGICR: SGI", v, info,
                                                gicr_reg, r);

    /* Read the pending status of an SGI is via GICR is not supported */
    case VREG32(GICR_ISPENDR0):
    case VREG32(GICR_ICPENDR0):
        goto read_as_zero;

    case VREG32(GICR_IGRPMODR0):
        /* We do not implement security extensions for guests, read zero */
        goto read_as_zero_32;

    case VREG32(GICR_NSACR):
        /* We do not implement security extensions for guests, read zero */
        goto read_as_zero_32;

    case 0x0E04 ... 0xBFFC:
        goto read_reserved;

    case 0xC000 ... 0xFFCC:
        goto read_impl_defined;

    case 0xFFD0 ... 0xFFFC:
        goto read_reserved;

    default:
        printk(XENLOG_G_ERR
               "%pv: vGICR: SGI: unhandled read r%d offset %#08x\n",
               v, dabt.reg, gicr_reg);
        goto read_as_zero;
    }
bad_width:
    printk(XENLOG_G_ERR "%pv: vGICR: SGI: bad read width %d r%d offset %#08x\n",
           v, dabt.size, dabt.reg, gicr_reg);
    return 0;

read_as_zero_32:
    if ( dabt.size != DABT_WORD ) goto bad_width;
read_as_zero:
    *r = 0;
    return 1;

read_impl_defined:
    printk(XENLOG_G_DEBUG
           "%pv: vGICR: SGI: RAZ on implementation defined register offset %#08x\n",
           v, gicr_reg);
    *r = 0;
    return 1;

read_reserved:
    printk(XENLOG_G_DEBUG
           "%pv: vGICR: SGI: RAZ on reserved register offset %#08x\n",
           v, gicr_reg);
    *r = 0;
    return 1;

}

static int vgic_v3_rdistr_sgi_mmio_write(struct vcpu *v, mmio_info_t *info,
                                         uint32_t gicr_reg, register_t r)
{
    struct hsr_dabt dabt = info->dabt;

    switch ( gicr_reg )
    {
    case VREG32(GICR_IGROUPR0):
    case VREG32(GICR_ISENABLER0):
    case VREG32(GICR_ICENABLER0):
    case VREG32(GICR_ISACTIVER0):
    case VREG32(GICR_ICACTIVER0):
    case VREG32(GICR_ICFGR1):
    case VRANGE32(GICR_IPRIORITYR0, GICR_IPRIORITYR7):
    case VREG32(GICR_ISPENDR0):
         /*
          * Above registers offset are common with GICD.
          * So handle common with GICD handling
          */
        return __vgic_v3_distr_common_mmio_write("vGICR: SGI", v,
                                                 info, gicr_reg, r);

    case VREG32(GICR_ICPENDR0):
        return __vgic_v3_distr_common_mmio_write("vGICR: SGI", v,
                                                 info, gicr_reg, r);

    case VREG32(GICR_IGRPMODR0):
        /* We do not implement security extensions for guests, write ignore */
        goto write_ignore_32;


    case VREG32(GICR_NSACR):
        /* We do not implement security extensions for guests, write ignore */
        goto write_ignore_32;

    default:
        printk(XENLOG_G_ERR
               "%pv: vGICR: SGI: unhandled write r%d offset %#08x\n",
               v, dabt.reg, gicr_reg);
        goto write_ignore;
    }

bad_width:
    printk(XENLOG_G_ERR
           "%pv: vGICR: SGI: bad write width %d r%d=%"PRIregister" offset %#08x\n",
           v, dabt.size, dabt.reg, r, gicr_reg);
    return 0;

write_ignore_32:
    if ( dabt.size != DABT_WORD ) goto bad_width;
    return 1;

write_ignore:
    return 1;
}

static struct vcpu *get_vcpu_from_rdist(struct domain *d,
    const struct vgic_rdist_region *region,
    paddr_t gpa, uint32_t *offset)
{
    struct vcpu *v;
    unsigned int vcpu_id;

    vcpu_id = region->first_cpu + ((gpa - region->base) / GICV3_GICR_SIZE);
    if ( unlikely(vcpu_id >= d->max_vcpus) )
        return NULL;

    v = d->vcpu[vcpu_id];

    *offset = gpa - v->arch.vgic.rdist_base;

    return v;
}

static int vgic_v3_rdistr_mmio_read(struct vcpu *v, mmio_info_t *info,
                                    register_t *r, void *priv)
{
    uint32_t offset;
    const struct vgic_rdist_region *region = priv;

    perfc_incr(vgicr_reads);

    v = get_vcpu_from_rdist(v->domain, region, info->gpa, &offset);
    if ( unlikely(!v) )
        return 0;

    if ( offset < SZ_64K )
        return __vgic_v3_rdistr_rd_mmio_read(v, info, offset, r);
    else  if ( (offset >= SZ_64K) && (offset < 2 * SZ_64K) )
        return vgic_v3_rdistr_sgi_mmio_read(v, info, (offset - SZ_64K), r);
    else
        printk(XENLOG_G_WARNING
               "%pv: vGICR: unknown gpa read address %"PRIpaddr"\n",
                v, info->gpa);

    return 0;
}

static int vgic_v3_rdistr_mmio_write(struct vcpu *v, mmio_info_t *info,
                                     register_t r, void *priv)
{
    uint32_t offset;
    const struct vgic_rdist_region *region = priv;

    perfc_incr(vgicr_writes);

    v = get_vcpu_from_rdist(v->domain, region, info->gpa, &offset);
    if ( unlikely(!v) )
        return 0;

    if ( offset < SZ_64K )
        return __vgic_v3_rdistr_rd_mmio_write(v, info, offset, r);
    else  if ( (offset >= SZ_64K) && (offset < 2 * SZ_64K) )
        return vgic_v3_rdistr_sgi_mmio_write(v, info, (offset - SZ_64K), r);
    else
        printk(XENLOG_G_WARNING
               "%pv: vGICR: unknown gpa write address %"PRIpaddr"\n",
               v, info->gpa);

    return 0;
}

static int vgic_v3_distr_mmio_read(struct vcpu *v, mmio_info_t *info,
                                   register_t *r, void *priv)
{
    struct hsr_dabt dabt = info->dabt;
    struct vgic_irq_rank *rank;
    unsigned long flags;
    int gicd_reg = (int)(info->gpa - v->domain->arch.vgic.dbase);

    perfc_incr(vgicd_reads);

    switch ( gicd_reg )
    {
    case VREG32(GICD_CTLR):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        vgic_lock(v);
        *r = vreg_reg32_extract(v->domain->arch.vgic.ctlr, info);
        vgic_unlock(v);
        return 1;

    case VREG32(GICD_TYPER):
    {
        /*
         * Number of interrupt identifier bits supported by the GIC
         * Stream Protocol Interface
         */
        /*
         * Number of processors that may be used as interrupt targets when ARE
         * bit is zero. The maximum is 8.
         */
        unsigned int ncpus = min_t(unsigned int, v->domain->max_vcpus, 8);
        uint32_t typer;

        if ( dabt.size != DABT_WORD ) goto bad_width;
        /* No secure world support for guests. */
        typer = ((ncpus - 1) << GICD_TYPE_CPUS_SHIFT |
                 DIV_ROUND_UP(v->domain->arch.vgic.nr_spis, 32));

        if ( v->domain->arch.vgic.has_its )
            typer |= GICD_TYPE_LPIS;

        typer |= (v->domain->arch.vgic.intid_bits - 1) << GICD_TYPE_ID_BITS_SHIFT;

        *r = vreg_reg32_extract(typer, info);

        return 1;
    }

    case VREG32(GICD_IIDR):
        if ( dabt.size != DABT_WORD ) goto bad_width;
        *r = vreg_reg32_extract(GICV3_GICD_IIDR_VAL, info);
        return 1;

    case VREG32(0x000C):
        goto read_reserved;

    case VREG32(GICD_STATUSR):
        /*
         *  Optional, Not implemented for now.
         *  Update to support guest debugging.
         */
        goto read_as_zero_32;

    case VRANGE32(0x0014, 0x001C):
        goto read_reserved;

    case VRANGE32(0x0020, 0x003C):
        goto read_impl_defined;

    case VREG32(GICD_SETSPI_NSR):
        /* Message based SPI is not implemented */
        goto read_reserved;

    case VREG32(0x0044):
        goto read_reserved;

    case VREG32(GICD_CLRSPI_NSR):
        /* Message based SPI is not implemented */
        goto read_reserved;

    case VREG32(0x004C):
        goto read_reserved;

    case VREG32(GICD_SETSPI_SR):
        /* Message based SPI is not implemented */
        goto read_reserved;

    case VREG32(0x0054):
        goto read_reserved;

    case VREG32(GICD_CLRSPI_SR):
        /* Message based SPI is not implemented */
        goto read_reserved;

    case VRANGE32(0x005C, 0x007C):
        goto read_reserved;

    case VRANGE32(GICD_IGROUPR, GICD_IGROUPRN):
    case VRANGE32(GICD_ISENABLER, GICD_ISENABLERN):
    case VRANGE32(GICD_ICENABLER, GICD_ICENABLERN):
    case VRANGE32(GICD_ISPENDR, GICD_ISPENDRN):
    case VRANGE32(GICD_ICPENDR, GICD_ICPENDRN):
    case VRANGE32(GICD_ISACTIVER, GICD_ISACTIVERN):
    case VRANGE32(GICD_ICACTIVER, GICD_ICACTIVERN):
    case VRANGE32(GICD_IPRIORITYR, GICD_IPRIORITYRN):
    case VRANGE32(GICD_ICFGR, GICD_ICFGRN):
    case VRANGE32(GICD_IGRPMODR, GICD_IGRPMODRN):
        /*
         * Above all register are common with GICR and GICD
         * Manage in common
         */
        return __vgic_v3_distr_common_mmio_read("vGICD", v, info, gicd_reg, r);

    case VRANGE32(GICD_NSACR, GICD_NSACRN):
        /* We do not implement security extensions for guests, read zero */
        goto read_as_zero_32;

    case VREG32(GICD_SGIR):
        /* Read as ICH_SGIR system register with SRE set. So ignore */
        goto read_as_zero_32;

    case VRANGE32(GICD_CPENDSGIR, GICD_CPENDSGIRN):
        /* Replaced with GICR_ICPENDR0. So ignore write */
        goto read_as_zero_32;

    case VRANGE32(GICD_SPENDSGIR, GICD_SPENDSGIRN):
        /* Replaced with GICR_ISPENDR0. So ignore write */
        goto read_as_zero_32;

    case VRANGE32(0x0F30, 0x60FC):
        goto read_reserved;

    case VRANGE64(GICD_IROUTER32, GICD_IROUTER1019):
    {
        uint64_t irouter;

        if ( !vgic_reg64_check_access(dabt) ) goto bad_width;
        rank = vgic_rank_offset(v, 64, gicd_reg - GICD_IROUTER,
                                DABT_DOUBLE_WORD);
        if ( rank == NULL ) goto read_as_zero;
        vgic_lock_rank(v, rank, flags);
        irouter = vgic_fetch_irouter(rank, gicd_reg - GICD_IROUTER);
        vgic_unlock_rank(v, rank, flags);

        *r = vreg_reg64_extract(irouter, info);

        return 1;
    }

    case VRANGE32(0x7FE0, 0xBFFC):
        goto read_reserved;

    case VRANGE32(0xC000, 0xFFCC):
        goto read_impl_defined;

    case VRANGE32(0xFFD0, 0xFFE4):
        /* Implementation defined identification registers */
       goto read_impl_defined;

    case VREG32(GICD_PIDR2):
        /* GICv3 identification value */
        if ( dabt.size != DABT_WORD ) goto bad_width;
        *r = vreg_reg32_extract(GICV3_GICD_PIDR2, info);
        return 1;

    case VRANGE32(0xFFEC, 0xFFFC):
         /* Implementation defined identification registers */
         goto read_impl_defined;

    default:
        printk(XENLOG_G_ERR "%pv: vGICD: unhandled read r%d offset %#08x\n",
               v, dabt.reg, gicd_reg);
        goto read_as_zero;
    }

bad_width:
    printk(XENLOG_G_ERR "%pv: vGICD: bad read width %d r%d offset %#08x\n",
           v, dabt.size, dabt.reg, gicd_reg);
    return 0;

read_as_zero_32:
    if ( dabt.size != DABT_WORD ) goto bad_width;
    *r = 0;
    return 1;

read_as_zero:
    *r = 0;
    return 1;

read_impl_defined:
    printk(XENLOG_G_DEBUG
           "%pv: vGICD: RAZ on implementation defined register offset %#08x\n",
           v, gicd_reg);
    *r = 0;
    return 1;

read_reserved:
    printk(XENLOG_G_DEBUG
           "%pv: vGICD: RAZ on reserved register offset %#08x\n",
           v, gicd_reg);
    *r = 0;
    return 1;
}

static int vgic_v3_distr_mmio_write(struct vcpu *v, mmio_info_t *info,
                                    register_t r, void *priv)
{
    struct hsr_dabt dabt = info->dabt;
    struct vgic_irq_rank *rank;
    unsigned long flags;
    int gicd_reg = (int)(info->gpa - v->domain->arch.vgic.dbase);

    perfc_incr(vgicd_writes);

    switch ( gicd_reg )
    {
    case VREG32(GICD_CTLR):
    {
        uint32_t ctlr = 0;

        if ( dabt.size != DABT_WORD ) goto bad_width;

        vgic_lock(v);

        vreg_reg32_update(&ctlr, r, info);

        /* Only EnableGrp1A can be changed */
        if ( ctlr & GICD_CTLR_ENABLE_G1A )
            v->domain->arch.vgic.ctlr |= GICD_CTLR_ENABLE_G1A;
        else
            v->domain->arch.vgic.ctlr &= ~GICD_CTLR_ENABLE_G1A;
        vgic_unlock(v);

        return 1;
    }

    case VREG32(GICD_TYPER):
        /* RO -- write ignored */
        goto write_ignore_32;

    case VREG32(GICD_IIDR):
        /* RO -- write ignored */
        goto write_ignore_32;

    case VREG32(0x000C):
        goto write_reserved;

    case VREG32(GICD_STATUSR):
        /* RO -- write ignored */
        goto write_ignore_32;

    case VRANGE32(0x0014, 0x001C):
        goto write_reserved;

    case VRANGE32(0x0020, 0x003C):
        goto write_impl_defined;

    case VREG32(GICD_SETSPI_NSR):
        /* Message based SPI is not implemented */
        goto write_reserved;

    case VREG32(0x0044):
        goto write_reserved;

    case VREG32(GICD_CLRSPI_NSR):
        /* Message based SPI is not implemented */
        goto write_reserved;

    case VREG32(0x004C):
        goto write_reserved;

    case VREG32(GICD_SETSPI_SR):
        /* Message based SPI is not implemented */
        goto write_reserved;

    case VREG32(0x0054):
        goto write_reserved;

    case VREG32(GICD_CLRSPI_SR):
        /* Message based SPI is not implemented */
        goto write_reserved;

    case VRANGE32(0x005C, 0x007C):
        goto write_reserved;

    case VRANGE32(GICD_IGROUPR, GICD_IGROUPRN):
    case VRANGE32(GICD_ISENABLER, GICD_ISENABLERN):
    case VRANGE32(GICD_ICENABLER, GICD_ICENABLERN):
    case VRANGE32(GICD_ISPENDR, GICD_ISPENDRN):
    case VRANGE32(GICD_ICPENDR, GICD_ICPENDRN):
    case VRANGE32(GICD_ISACTIVER, GICD_ISACTIVERN):
    case VRANGE32(GICD_ICACTIVER, GICD_ICACTIVERN):
    case VRANGE32(GICD_IPRIORITYR, GICD_IPRIORITYRN):
    case VRANGE32(GICD_ICFGR, GICD_ICFGRN):
    case VRANGE32(GICD_IGRPMODR, GICD_IGRPMODRN):
        /* Above registers are common with GICR and GICD
         * Manage in common */
        return __vgic_v3_distr_common_mmio_write("vGICD", v, info,
                                                 gicd_reg, r);

    case VRANGE32(GICD_NSACR, GICD_NSACRN):
        /* We do not implement security extensions for guests, write ignore */
        goto write_ignore_32;

    case VREG32(GICD_SGIR):
        /* it is accessed as system register in GICv3 */
        goto write_ignore_32;

    case VRANGE32(GICD_CPENDSGIR, GICD_CPENDSGIRN):
        /* Replaced with GICR_ICPENDR0. So ignore write */
        if ( dabt.size != DABT_WORD ) goto bad_width;
        return 0;

    case VRANGE32(GICD_SPENDSGIR, GICD_SPENDSGIRN):
        /* Replaced with GICR_ISPENDR0. So ignore write */
        if ( dabt.size != DABT_WORD ) goto bad_width;
        return 0;

    case VRANGE32(0x0F30, 0x60FC):
        goto write_reserved;

    case VRANGE64(GICD_IROUTER32, GICD_IROUTER1019):
    {
        uint64_t irouter;

        if ( !vgic_reg64_check_access(dabt) ) goto bad_width;
        rank = vgic_rank_offset(v, 64, gicd_reg - GICD_IROUTER,
                                DABT_DOUBLE_WORD);
        if ( rank == NULL ) goto write_ignore;
        vgic_lock_rank(v, rank, flags);
        irouter = vgic_fetch_irouter(rank, gicd_reg - GICD_IROUTER);
        vreg_reg64_update(&irouter, r, info);
        vgic_store_irouter(v->domain, rank, gicd_reg - GICD_IROUTER, irouter);
        vgic_unlock_rank(v, rank, flags);
        return 1;
    }

    case VRANGE32(0x7FE0, 0xBFFC):
        goto write_reserved;

    case VRANGE32(0xC000, 0xFFCC):
        goto write_impl_defined;

    case VRANGE32(0xFFD0, 0xFFE4):
        /* Implementation defined identification registers */
       goto write_impl_defined;

    case VREG32(GICD_PIDR2):
        /* RO -- write ignore */
        goto write_ignore_32;

    case VRANGE32(0xFFEC, 0xFFFC):
         /* Implementation defined identification registers */
         goto write_impl_defined;

    default:
        printk(XENLOG_G_ERR
               "%pv: vGICD: unhandled write r%d=%"PRIregister" offset %#08x\n",
               v, dabt.reg, r, gicd_reg);
        goto write_ignore;
    }

bad_width:
    printk(XENLOG_G_ERR
           "%pv: vGICD: bad write width %d r%d=%"PRIregister" offset %#08x\n",
           v, dabt.size, dabt.reg, r, gicd_reg);
    return 0;

write_ignore_32:
    if ( dabt.size != DABT_WORD ) goto bad_width;
    return 1;

write_ignore:
    return 1;

write_impl_defined:
    printk(XENLOG_G_DEBUG
           "%pv: vGICD: WI on implementation defined register offset %#08x\n",
           v, gicd_reg);
    return 1;

write_reserved:
    printk(XENLOG_G_DEBUG
           "%pv: vGICD: WI on reserved register offset %#08x\n",
           v, gicd_reg);
    return 1;
}

static bool vgic_v3_to_sgi(struct vcpu *v, uint64_t sgir)
{
    int virq;
    int irqmode;
    enum gic_sgi_mode sgi_mode;
    struct sgi_target target;

    sgi_target_init(&target);
    irqmode = (sgir >> ICH_SGI_IRQMODE_SHIFT) & ICH_SGI_IRQMODE_MASK;
    virq = (sgir >> ICH_SGI_IRQ_SHIFT ) & ICH_SGI_IRQ_MASK;

    /* Map GIC sgi value to enum value */
    switch ( irqmode )
    {
    case ICH_SGI_TARGET_LIST:
        /* We assume that only AFF1 is used in ICC_SGI1R_EL1. */
        target.aff1 = (sgir >> ICH_SGI_AFFINITY_LEVEL(1)) & ICH_SGI_AFFx_MASK;
        target.list = sgir & ICH_SGI_TARGETLIST_MASK;
        sgi_mode = SGI_TARGET_LIST;
        break;
    case ICH_SGI_TARGET_OTHERS:
        sgi_mode = SGI_TARGET_OTHERS;
        break;
    default:
        gprintk(XENLOG_WARNING, "Wrong irq mode in SGI1R_EL1 register\n");
        return false;
    }

    return vgic_to_sgi(v, sgir, sgi_mode, virq, &target);
}

static bool vgic_v3_emulate_sgi1r(struct cpu_user_regs *regs, uint64_t *r,
                                  bool read)
{
    /* WO */
    if ( !read )
        return vgic_v3_to_sgi(current, *r);
    else
    {
        gdprintk(XENLOG_WARNING, "Reading SGI1R_EL1 - WO register\n");
        return false;
    }
}

#ifdef CONFIG_ARM_64
static bool vgic_v3_emulate_sysreg(struct cpu_user_regs *regs, union hsr hsr)
{
    struct hsr_sysreg sysreg = hsr.sysreg;

    ASSERT (hsr.ec == HSR_EC_SYSREG);

    if ( sysreg.read )
        perfc_incr(vgic_sysreg_reads);
    else
        perfc_incr(vgic_sysreg_writes);

    switch ( hsr.bits & HSR_SYSREG_REGS_MASK )
    {
    case HSR_SYSREG_ICC_SGI1R_EL1:
        return vreg_emulate_sysreg(regs, hsr, vgic_v3_emulate_sgi1r);

    default:
        return false;
    }
}
#endif

static bool vgic_v3_emulate_cp64(struct cpu_user_regs *regs, union hsr hsr)
{
    struct hsr_cp64 cp64 = hsr.cp64;

    if ( cp64.read )
        perfc_incr(vgic_cp64_reads);
    else
        perfc_incr(vgic_cp64_writes);

    switch ( hsr.bits & HSR_CP64_REGS_MASK )
    {
    case HSR_CPREG64(ICC_SGI1R):
        return vreg_emulate_cp64(regs, hsr, vgic_v3_emulate_sgi1r);
    default:
        return false;
    }
}

static bool vgic_v3_emulate_reg(struct cpu_user_regs *regs, union hsr hsr)
{
    switch (hsr.ec)
    {
#ifdef CONFIG_ARM_64
    case HSR_EC_SYSREG:
        return vgic_v3_emulate_sysreg(regs, hsr);
#endif
    case HSR_EC_CP15_64:
        return vgic_v3_emulate_cp64(regs, hsr);
    default:
        return false;
    }
}

static const struct mmio_handler_ops vgic_rdistr_mmio_handler = {
    .read  = vgic_v3_rdistr_mmio_read,
    .write = vgic_v3_rdistr_mmio_write,
};

static const struct mmio_handler_ops vgic_distr_mmio_handler = {
    .read  = vgic_v3_distr_mmio_read,
    .write = vgic_v3_distr_mmio_write,
};

static int vgic_v3_vcpu_init(struct vcpu *v)
{
    int i;
    paddr_t rdist_base;
    struct vgic_rdist_region *region;
    unsigned int last_cpu;

    /* Convenient alias */
    struct domain *d = v->domain;

    /*
     * Find the region where the re-distributor lives. For this purpose,
     * we look one region ahead as we have only the first CPU in hand.
     */
    for ( i = 1; i < d->arch.vgic.nr_regions; i++ )
    {
        if ( v->vcpu_id < d->arch.vgic.rdist_regions[i].first_cpu )
            break;
    }

    region = &d->arch.vgic.rdist_regions[i - 1];

    /* Get the base address of the redistributor */
    rdist_base = region->base;
    rdist_base += (v->vcpu_id - region->first_cpu) * GICV3_GICR_SIZE;

    /* Check if a valid region was found for the re-distributor */
    if ( (rdist_base < region->base) ||
         ((rdist_base + GICV3_GICR_SIZE) > (region->base + region->size)) )
    {
        dprintk(XENLOG_ERR,
                "d%u: Unable to find a re-distributor for VCPU %u\n",
                d->domain_id, v->vcpu_id);
        return -EINVAL;
    }

    v->arch.vgic.rdist_base = rdist_base;

    /*
     * If the redistributor is the last one of the
     * contiguous region of the vCPU is the last of the domain, set
     * VGIC_V3_RDIST_LAST flags.
     * Note that we are assuming max_vcpus will never change.
     */
    last_cpu = (region->size / GICV3_GICR_SIZE) + region->first_cpu - 1;

    if ( v->vcpu_id == last_cpu || (v->vcpu_id == (d->max_vcpus - 1)) )
        v->arch.vgic.flags |= VGIC_V3_RDIST_LAST;

    return 0;
}

/*
 * Return the maximum number possible of re-distributor regions for
 * a given domain.
 */
static inline unsigned int vgic_v3_max_rdist_count(struct domain *d)
{
    /*
     * Normally there is only one GICv3 redistributor region.
     * The GICv3 DT binding provisions for multiple regions, since there are
     * platforms out there which need those (multi-socket systems).
     * For domain using the host memory layout, we have to live with the MMIO
     * layout the hardware provides, so we have to copy the multiple regions
     * - as the first region may not provide enough space to hold all
     * redistributors we need.
     * All the other domains will get a constructed memory map, so we can go
     * with the architected single redistributor region.
     */
    return domain_use_host_layout(d) ? vgic_v3_hw.nr_rdist_regions :
                                       GUEST_GICV3_RDIST_REGIONS;
}

static int vgic_v3_domain_init(struct domain *d)
{
    struct vgic_rdist_region *rdist_regions;
    int rdist_count, i, ret;

    /* Allocate memory for Re-distributor regions */
    rdist_count = vgic_v3_max_rdist_count(d);

    rdist_regions = xzalloc_array(struct vgic_rdist_region, rdist_count);
    if ( !rdist_regions )
        return -ENOMEM;

    d->arch.vgic.nr_regions = rdist_count;
    d->arch.vgic.rdist_regions = rdist_regions;

    rwlock_init(&d->arch.vgic.pend_lpi_tree_lock);
    radix_tree_init(&d->arch.vgic.pend_lpi_tree);

    /*
     * For domain using the host memory layout, it gets the hardware
     * address.
     * Other domains get the virtual platform layout.
     */
    if ( domain_use_host_layout(d) )
    {
        unsigned int first_cpu = 0;

        d->arch.vgic.dbase = vgic_v3_hw.dbase;

        for ( i = 0; i < vgic_v3_hw.nr_rdist_regions; i++ )
        {
            paddr_t size = vgic_v3_hw.regions[i].size;

            d->arch.vgic.rdist_regions[i].base = vgic_v3_hw.regions[i].base;
            d->arch.vgic.rdist_regions[i].size = size;

            /* Set the first CPU handled by this region */
            d->arch.vgic.rdist_regions[i].first_cpu = first_cpu;

            first_cpu += size / GICV3_GICR_SIZE;

            if ( first_cpu >= d->max_vcpus )
                break;
        }

        /*
         * For domain using the host memory layout, it may not use all
         * the re-distributors regions (e.g when the number of vCPUs does
         * not match the number of pCPUs). Update the number of regions to
         * avoid exposing unused region as they will not get emulated.
         */
        d->arch.vgic.nr_regions = i + 1;

        d->arch.vgic.intid_bits = vgic_v3_hw.intid_bits;
    }
    else
    {
        d->arch.vgic.dbase = GUEST_GICV3_GICD_BASE;

        /* A single Re-distributor region is mapped for the guest. */
        BUILD_BUG_ON(GUEST_GICV3_RDIST_REGIONS != 1);

        /* The first redistributor should contain enough space for all CPUs */
        BUILD_BUG_ON((GUEST_GICV3_GICR0_SIZE / GICV3_GICR_SIZE) < MAX_VIRT_CPUS);
        d->arch.vgic.rdist_regions[0].base = GUEST_GICV3_GICR0_BASE;
        d->arch.vgic.rdist_regions[0].size = GUEST_GICV3_GICR0_SIZE;
        d->arch.vgic.rdist_regions[0].first_cpu = 0;

        /*
         * TODO: only SPIs for now, adjust this when guests need LPIs.
         * Please note that this value just describes the bits required
         * in the stream interface, which is of no real concern for our
         * emulation. So we just go with "10" here to cover all eventual
         * SPIs (even if the guest implements less).
         */
        d->arch.vgic.intid_bits = 10;
    }

    ret = vgic_v3_its_init_domain(d);
    if ( ret )
        return ret;

    /* Register mmio handle for the Distributor */
    register_mmio_handler(d, &vgic_distr_mmio_handler, d->arch.vgic.dbase,
                          SZ_64K, NULL);

    /*
     * Register mmio handler per contiguous region occupied by the
     * redistributors. The handler will take care to choose which
     * redistributor is targeted.
     */
    for ( i = 0; i < d->arch.vgic.nr_regions; i++ )
    {
        struct vgic_rdist_region *region = &d->arch.vgic.rdist_regions[i];

        register_mmio_handler(d, &vgic_rdistr_mmio_handler,
                              region->base, region->size, region);
    }

    d->arch.vgic.ctlr = VGICD_CTLR_DEFAULT;

    return 0;
}

static void vgic_v3_domain_free(struct domain *d)
{
    vgic_v3_its_free_domain(d);
    /*
     * It is expected that at this point all actual ITS devices have been
     * cleaned up already. The struct pending_irq's, for which the pointers
     * have been stored in the radix tree, are allocated and freed by device.
     * On device unmapping all the entries are removed from the tree and
     * the backing memory is freed.
     */
    radix_tree_destroy(&d->arch.vgic.pend_lpi_tree, NULL);
    xfree(d->arch.vgic.rdist_regions);
}

/*
 * Looks up a virtual LPI number in our tree of mapped LPIs. This will return
 * the corresponding struct pending_irq, which we also use to store the
 * enabled and pending bit plus the priority.
 * Returns NULL if an LPI cannot be found (or no LPIs are supported).
 */
static struct pending_irq *vgic_v3_lpi_to_pending(struct domain *d,
                                                  unsigned int lpi)
{
    struct pending_irq *pirq;

    read_lock(&d->arch.vgic.pend_lpi_tree_lock);
    pirq = radix_tree_lookup(&d->arch.vgic.pend_lpi_tree, lpi);
    read_unlock(&d->arch.vgic.pend_lpi_tree_lock);

    return pirq;
}

/* Retrieve the priority of an LPI from its struct pending_irq. */
static int vgic_v3_lpi_get_priority(struct domain *d, uint32_t vlpi)
{
    struct pending_irq *p = vgic_v3_lpi_to_pending(d, vlpi);

    ASSERT(p);

    return p->lpi_priority;
}

static const struct vgic_ops v3_ops = {
    .vcpu_init   = vgic_v3_vcpu_init,
    .domain_init = vgic_v3_domain_init,
    .domain_free = vgic_v3_domain_free,
    .emulate_reg  = vgic_v3_emulate_reg,
    .lpi_to_pending = vgic_v3_lpi_to_pending,
    .lpi_get_priority = vgic_v3_lpi_get_priority,
};

int vgic_v3_init(struct domain *d, unsigned int *mmio_count)
{
    if ( !vgic_v3_hw.enabled )
    {
        printk(XENLOG_G_ERR
               "d%d: vGICv3 is not supported on this platform.\n",
               d->domain_id);
        return -ENODEV;
    }

    /* GICD region + number of Redistributors */
    *mmio_count = vgic_v3_max_rdist_count(d) + 1;

    /* one region per ITS */
    *mmio_count += vgic_v3_its_count(d);

    register_vgic_ops(d, &v3_ops);

    return 0;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: vgic-v3.c === */
/* === BEGIN INLINED: softirq.c === */
#include <xen_xen_config.h>
/******************************************************************************
 * common/softirq.c
 * 
 * Softirqs in Xen are only executed in an outermost activation (e.g., never 
 * within an interrupt activation). This simplifies some things and generally 
 * seems a good thing.
 * 
 * Copyright (c) 2003, K A Fraser
 * Copyright (c) 1992, Linus Torvalds
 */

#include <xen_init.h>
#include <xen_mm.h>
#include <xen_preempt.h>
#include <xen_sched.h>
#include <xen_rcupdate.h>
#include <xen_softirq.h>

#ifndef __ARCH_IRQ_STAT
irq_cpustat_t irq_stat[NR_CPUS];
#endif

static softirq_handler softirq_handlers[NR_SOFTIRQS];

static DEFINE_PER_CPU(cpumask_t, batch_mask);
static DEFINE_PER_CPU(unsigned int, batching);

static void __do_softirq(unsigned long ignore_mask)
{
    unsigned int i, cpu;
    unsigned long pending;
    bool rcu_allowed = !(ignore_mask & (1UL << RCU_SOFTIRQ));

    ASSERT(!rcu_allowed || rcu_quiesce_allowed());

    for ( ; ; )
    {
        /*
         * Initialise @cpu on every iteration: SCHEDULE_SOFTIRQ or
         * SCHED_SLAVE_SOFTIRQ may move us to another processor.
         */
        cpu = smp_processor_id();

        if ( rcu_allowed && rcu_pending(cpu) )
            rcu_check_callbacks(cpu);

        if ( ((pending = (softirq_pending(cpu) & ~ignore_mask)) == 0)
             || cpu_is_offline(cpu) )
            break;

        i = ffsl(pending) - 1;
        clear_bit(i, &softirq_pending(cpu));
        (*softirq_handlers[i])();
    }
}

void process_pending_softirqs(void)
{
    /* Do not enter scheduler as it can preempt the calling context. */
    unsigned long ignore_mask = (1UL << SCHEDULE_SOFTIRQ) |
                                (1UL << SCHED_SLAVE_SOFTIRQ);

    /* Block RCU processing in case of rcu_read_lock() held. */
    if ( !rcu_quiesce_allowed() )
        ignore_mask |= 1UL << RCU_SOFTIRQ;

    ASSERT(!in_irq() && local_irq_is_enabled());
    __do_softirq(ignore_mask);
}

void do_softirq(void)
{
    ASSERT_NOT_IN_ATOMIC();
    __do_softirq(0);
}

void open_softirq(int nr, softirq_handler handler)
{
    ASSERT(nr < NR_SOFTIRQS);
    softirq_handlers[nr] = handler;
}

void cpumask_raise_softirq(const cpumask_t *mask, unsigned int nr)
{
    unsigned int cpu, this_cpu = smp_processor_id();
    cpumask_t send_mask, *raise_mask;

    if ( !per_cpu(batching, this_cpu) || in_irq() )
    {
        cpumask_clear(&send_mask);
        raise_mask = &send_mask;
    }
    else
        raise_mask = &per_cpu(batch_mask, this_cpu);

    for_each_cpu(cpu, mask)
        if ( !test_and_set_bit(nr, &softirq_pending(cpu)) &&
             cpu != this_cpu &&
             !arch_skip_send_event_check(cpu) )
            __cpumask_set_cpu(cpu, raise_mask);

    if ( raise_mask == &send_mask )
        smp_send_event_check_mask(raise_mask);
}

void cpu_raise_softirq(unsigned int cpu, unsigned int nr)
{
    unsigned int this_cpu = smp_processor_id();

    if ( test_and_set_bit(nr, &softirq_pending(cpu))
         || (cpu == this_cpu)
         || arch_skip_send_event_check(cpu) )
        return;

    if ( !per_cpu(batching, this_cpu) || in_irq() )
        smp_send_event_check_cpu(cpu);
    else
        __cpumask_set_cpu(cpu, &per_cpu(batch_mask, this_cpu));
}



void raise_softirq(unsigned int nr)
{
    set_bit(nr, &softirq_pending(smp_processor_id()));
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

/* === END INLINED: softirq.c === */
