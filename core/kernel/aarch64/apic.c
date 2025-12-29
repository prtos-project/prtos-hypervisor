/*
 * FILE: apic.c
 *
 * Local & IO Advanced Programming Interrupts Controller (APIC)
 * IOAPIC, A.K.A. i82093AA
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
#include <arch/atomic.h>
#include <arch/apic.h>
#include <arch/irqs.h>
#include <arch/processor.h>
#include <arch/pic.h>
#include <arch/io.h>

RESERVE_PHYSPAGES(LAPIC_DEFAULT_BASE, 1);
RESERVE_PHYSPAGES(IOAPIC_DEFAULT_BASE, 1);
static prtos_address_t lapic_addr;
static prtos_address_t ioapic_addr[CONFIG_MAX_NO_IOAPICS];
static spin_lock_t io_apic_lock = SPINLOCK_INIT;
#if CONFIG_MAX_NO_IOAPICS > 1
static prtos_u8_t io_irq_to_io_apic[CONFIG_MAX_NO_IOINT];
#endif

prtos_u32_t lapic_read(prtos_u32_t reg) {
    return *(volatile prtos_u32_t *)(lapic_addr + reg);
}

void lapic_write(prtos_u32_t reg, prtos_u32_t v) {
    *(volatile prtos_u32_t *)(lapic_addr + reg) = v;
}

void lapic_eoi(void) {
    lapic_write(APIC_EOI, 0);
}

static inline prtos_u32_t io_apic_read(prtos_u32_t reg, prtos_s32_t io_apic) {
    prtos_u32_t val;

    *((volatile prtos_u32_t *)(ioapic_addr[io_apic] + APIC_IOREGSEL)) = reg;
    val = *((volatile prtos_u32_t *)(ioapic_addr[io_apic] + APIC_IOWIN));

    return val;
}

static inline void io_apic_write(prtos_u32_t reg, prtos_u32_t v, prtos_s32_t io_apic) {
    spin_lock(&io_apic_lock);
    *((volatile prtos_u32_t *)(ioapic_addr[io_apic] + APIC_IOREGSEL)) = reg;
    *((volatile prtos_u32_t *)(ioapic_addr[io_apic] + APIC_IOWIN)) = v;
    spin_unlock(&io_apic_lock);
}

void io_apic_write_entry(prtos_u8_t pin, struct io_apic_route_entry *e, prtos_s32_t io_apic) {
    io_apic_write(0x11 + (pin * 2), *(((prtos_s32_t *)e) + 1), io_apic);
    io_apic_write(0x10 + (pin * 2), *(((prtos_s32_t *)e) + 0), io_apic);
}

struct io_apic_route_entry io_apic_read_entry(prtos_u8_t pin, prtos_s32_t io_apic) {
    struct io_apic_route_entry e;

    spin_lock(&io_apic_lock);
    *(((prtos_s32_t *)&e) + 1) = io_apic_read(0x11 + (pin * 2), io_apic);
    *(((prtos_s32_t *)&e) + 0) = io_apic_read(0x10 + (pin * 2), io_apic);
    spin_unlock(&io_apic_lock);

    return e;
}

static inline prtos_u32_t apic_wait_icr_idle(void) {
    prtos_u32_t send_status;
    prtos_s32_t timeout;

    // for (timeout = 0; timeout < 1000; ++timeout) {
    //     send_status = lapic_read(APIC_ICR_LOW) & APIC_ICR_BUSY;
    //     if (!send_status) break;
    //     early_delay(200);
    // }

    return send_status;
}

static inline prtos_u32_t get_lapic_id(void) {
    // prtos_u32_t id = lapic_read(APIC_ID);
    // return (((id) >> 24) & 0xFF);
}

static inline void io_apic_mask_irq(prtos_s32_t irq, prtos_s32_t io_apic) {
    // struct io_apic_route_entry entry;

    // entry = io_apic_read_entry(irq, io_apic);
    // entry.mask = 1;
    // io_apic_write_entry(irq, &entry, io_apic);
}

static inline void io_apic_unmask_irq(prtos_s32_t irq, prtos_s32_t io_apic) {
    // struct io_apic_route_entry entry;

    // entry = io_apic_read_entry(irq, io_apic);
    // entry.mask = 0;
    // io_apic_write_entry(irq, &entry, io_apic);
}

static inline void io_apic_level_irq(prtos_s32_t irq, prtos_s32_t io_apic) {
    // struct io_apic_route_entry entry;

    // entry = io_apic_read_entry(irq, io_apic);
    // entry.trigger = 1;
    // io_apic_write_entry(irq, &entry, io_apic);
}

static inline void io_apic_edge_irq(prtos_s32_t irq, prtos_s32_t io_apic) {
    // struct io_apic_route_entry entry;

    // entry = io_apic_read_entry(irq, io_apic);
    // entry.trigger = 0;
    // io_apic_write_entry(irq, &entry, io_apic);
}

#if defined(CONFIG_CHIPSET_ICH)
static inline void io_apic_eoi(prtos_s32_t irq, prtos_s32_t io_apic) {
    // *((volatile prtos_u32_t *)(ioapic_addr[io_apic] + IO_APIC_EOI)) = irq;
}
#else
static inline void io_apic_eoi(prtos_s32_t irq, prtos_s32_t io_apic) {}
#endif

static void io_apic_enable_irq(prtos_u32_t irq) {
    // io_apic_unmask_irq(irq, 0);
    // x86_hw_irqs_mask[GET_CPU_ID()] &= ~(1 << irq);
}

static void io_apic_disable_irq(prtos_u32_t irq) {
    // x86_hw_irqs_mask[GET_CPU_ID()] |= 1 << irq;
}

#ifdef CONFIG_APIC
void hw_irq_set_mask(prtos_s32_t e, prtos_u32_t mask) {
#if 0
    prtos_s32_t e;
    hw_cli();
    for (e=0; e<CONFIG_NO_HWIRQS; e++) {
        if (mask&(1<<e))
            hw_disable_irq(e);
        else
            hw_enable_irq(e);
    }
    hw_sti();
#endif
}
#endif

void io_apic_ack_edge_irq(prtos_u32_t irq) {
    // lapic_eoi();
}

void io_apic_ack_level_irq(prtos_u32_t irq) {
    // prtos_u32_t vector, mask;

    // io_apic_mask_irq(irq, 0);

    // vector = irq + FIRST_EXTERNAL_VECTOR;
    // mask = lapic_read(APIC_TMR + ((vector & ~0x1f) >> 1));

    // lapic_eoi();

    // if (!(mask & (1 << (mask & 0x1f)))) {
    //     io_apic_eoi(irq, 0);
    // }
}

static void lvtt_enable_irq(prtos_u32_t irq) {
    // lapic_write(APIC_LVTT, ~APIC_LVT_MASKED & lapic_read(APIC_LVTT));
    // x86_hw_irqs_mask[GET_CPU_ID()] &= ~(1 << LAPIC_TIMER_IRQ);
}

static void lvtt_disablentry_irq(prtos_u32_t irq) {
    // lapic_write(APIC_LVTT, APIC_LVT_MASKED | lapic_read(APIC_LVTT));
    // x86_hw_irqs_mask[GET_CPU_ID()] |= 1 << LAPIC_TIMER_IRQ;
}

static void lvtt_mask_and_ack_irq(prtos_u32_t irq) {
    // lapic_write(APIC_LVTT, APIC_LVT_MASKED | lapic_read(APIC_LVTT));
    // lapic_write(APIC_EOI, 0);
    // x86_hw_irqs_mask[GET_CPU_ID()] |= 1 << LAPIC_TIMER_IRQ;
}

static inline prtos_s32_t find_irq_vector(prtos_s32_t int_number) {
    return FIRST_EXTERNAL_VECTOR + int_number;
}

static inline void __VBOOT print_entry(prtos_s32_t int_number, struct io_apic_route_entry *io_apic_entry) {
    // eprintf("APIC entry %d, %d, %d, %d\n", int_number, io_apic_entry->vector, io_apic_entry->trigger, io_apic_entry->delivery_mode);
}

static inline void __VBOOT get_apic_version(void) {
    // struct io_apic_versionsion_entry apic_version;

    // apic_version.raw = io_apic_read(IO_APIC_VER, 0);
    // eprintf("Version %d Entries %d\n", apic_version.version, apic_version.entries);
}

static inline void __VBOOT setup_io_apic_entry(prtos_s32_t irq, prtos_s32_t apic, prtos_s32_t trigger, prtos_s32_t polarity) {
    // struct io_apic_route_entry io_apic_entry;

    // io_apic_entry = io_apic_read_entry(irq, apic);
    // io_apic_entry.delivery_mode = dest_fixed;
    // io_apic_entry.dest_mode = 1;
    // io_apic_entry.dest.logical.logical_dest = 0xf;
    // io_apic_entry.mask |= 1;
    // io_apic_entry.vector = find_irq_vector(irq);
    // io_apic_entry.trigger = trigger;
    // io_apic_entry.polarity = polarity;
    // io_apic_write_entry(irq, &io_apic_entry, apic);

    // hw_irq_ctrl[irq].enable = io_apic_enable_irq;
    // hw_irq_ctrl[irq].disable = io_apic_disable_irq;
    // hw_irq_ctrl[irq].end = io_apic_disable_irq;
    // if (io_apic_entry.trigger == IO_APIC_TRIG_LEVEL) {
    //     hw_irq_ctrl[irq].ack = io_apic_ack_level_irq;
    // } else if (io_apic_entry.trigger == IO_APIC_TRIG_EDGE) {
    //     hw_irq_ctrl[irq].ack = io_apic_ack_edge_irq;
    // }
}

static inline void __VBOOT init_io_apic(void) {
//     prtos_s32_t e, i;

//     if (x86_mp_conf.num_of_io_apic != CONFIG_MAX_NO_IOAPICS) {
//         x86system_panic("Only supported %d IO-APIC, found %d", CONFIG_MAX_NO_IOAPICS, x86_mp_conf.num_of_io_apic);
//     }

//     for (e = 0; e < x86_mp_conf.num_of_io_apic; e++) {
//         ioapic_addr[e] = vmm_alloc(1);
//         vm_map_page(x86_mp_conf.io_apic[e].base_addr, ioapic_addr[e], _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_GLOBAL | _PG_ARCH_PCD);
//     }
//     apic_wait_icr_idle();
//     lapic_write(APIC_ICR_LOW, APIC_DEST_ALLINC | APIC_INT_LEVELTRIG | APIC_DM_INIT);

//     // Disabling all PIC interrupts
//     for (e = 0; e < PIC_IRQS; e++) hw_disable_irq(e);

// #if defined(CONFIG_CHIPSET_ICH)
//     io_apic_write(IO_APIC_ID, (x86_mp_conf.io_apic[0].id & 0xff) << 24, 0);
//     for (i = 0; i < PIC_IRQS; i++) {
//         setup_io_apic_entry(i, 0, IO_APIC_TRIG_EDGE, IO_APIC_POL_HIGH);
//         hw_disable_irq(i);
//     }
//     for (i = PIC_IRQS; i < 23; ++i) {
//         setup_io_apic_entry(i, 0, IO_APIC_TRIG_LEVEL, IO_APIC_POL_LOW);
//         hw_disable_irq(i);
//     }
// #else
//     for (e = 0; e < x86_mp_conf.num_of_io_apic; ++e) {
//         io_apic_write(IO_APIC_ID, (x86_mp_conf.io_apic[e].id & 0xff) << 24, e);
//         for (i = 0; i < x86_mp_conf.num_of_io_int; i++) {
//             if (x86_mp_conf.io_int[i].dst_io_apic_id == x86_mp_conf.io_apic[e].id) {
//                 setup_io_apic_entry(i, e, IO_APIC_TRIG_EDGE, IO_APIC_POL_HIGH);
//             }
//         }
//         for (i = 0; i < x86_mp_conf.num_of_io_int; i++) {
//             hw_disable_irq(i);
//         }
//     }
// #endif
}

static inline void init_lapic_map(void) {
    // prtos_u64_t apic_msr;
    // prtos_address_t lapic_phys_addr;

    // apic_msr = read_msr(MSR_IA32_APIC_BASE);
    // if (!(apic_msr & _MSR_APICBASE_ENABLE)) x86system_panic("APIC not supported");

    // lapic_phys_addr = apic_msr & APIC_BASE_MASK_MSR;
    // if (lapic_phys_addr != LAPIC_DEFAULT_BASE) x86system_panic("APIC base address (0x%x) not expected", lapic_phys_addr);

    // lapic_addr = vmm_alloc(1);
    // vm_map_page(lapic_phys_addr, lapic_addr, _PG_ARCH_PRESENT | _PG_ARCH_RW | _PG_ARCH_GLOBAL | _PG_ARCH_PCD);
}

void send_ipi(prtos_u8_t dst, prtos_u8_t dst_short_hand, prtos_u8_t vector) {
    prtos_u32_t h_ipi, l_ipi;

    // h_ipi = ((local_id_table[dst].hw_id) << 24);
    // l_ipi = ((dst_short_hand & 0x3) << 18) | vector;
    // lapic_write(APIC_ICR_HIGH, h_ipi);
    // lapic_write(APIC_ICR_LOW, l_ipi);
}

void __VBOOT init_lapic(prtos_s32_t cpu_id) {
    // prtos_u32_t v;

    // lapic_write(APIC_DFR, APIC_DFR_FLAT);
    // v = (1 << cpu_id) << 24;
    // lapic_write(APIC_LDR, v);
    // v = lapic_read(APIC_TASKPRI);
    // v &= ~APIC_TPRI_MASK;
    // lapic_write(APIC_TASKPRI, v);

    // v = lapic_read(APIC_SVR);
    // v &= ~APIC_VECTOR_MASK;
    // v |= APIC_SVR_APIC_ENABLED;
    // v |= APIC_SVR_FOCUS_DISABLED;
    // lapic_write(APIC_SVR, v);

    // lapic_write(APIC_ESR, 0);
    // lapic_write(APIC_ESR, 0);
    // lapic_write(APIC_ESR, 0);
    // lapic_write(APIC_ESR, 0);
    // lapic_write(APIC_LVTERR, APIC_LVT_MASKED);
    // lapic_write(APIC_LVTPC, APIC_LVT_MASKED);
    // lapic_write(APIC_LVTT, APIC_LVT_MASKED);
    // lapic_write(APIC_LVT0, APIC_LVT_MASKED);
    // lapic_write(APIC_LVT1, APIC_LVT_MASKED);
}

void __VBOOT setup_apic_common(void) {
    prtos_u64_t msr;

    // msr = read_msr(MSR_IA32_APIC_BASE);
    // if (!(msr & _MSR_APICBASE_ENABLE)) {
    //     kprintf("[FATAL] APIC not enabled\n", 0);
    //     return;
    // }
    // if (x86_mp_conf.imcr) {
    //     out_byte(0x70, 0x22);
    //     out_byte(0x00, 0x23);
    // }

    // init_io_apic();
    // init_lapic_map();

    // hw_irq_ctrl[LAPIC_TIMER_IRQ].enable = lvtt_enable_irq;
    // hw_irq_ctrl[LAPIC_TIMER_IRQ].disable = lvtt_disablentry_irq;
    // hw_irq_ctrl[LAPIC_TIMER_IRQ].ack = lvtt_mask_and_ack_irq;
    // hw_irq_ctrl[LAPIC_TIMER_IRQ].end = lvtt_enable_irq;
    // hw_irq_ctrl[SCHED_PENDING_IPI_IRQ].ack = io_apic_ack_edge_irq;
}
