/*
 * FILE: apic.h
 *
 * The PC's APIC
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_APIC_H_
#define _PRTOS_ARCH_APIC_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#define LAPIC_DEFAULT_BASE 0xfee00000
#define IOAPIC_DEFAULT_BASE 0xfec00000

#define LAPIC_IRQ(X) ((X) + CONFIG_MAX_NO_IOINT)
#define IOAPIC_IRQ(X) (X)

#define LAPIC_TIMER_IRQ LAPIC_IRQ(2)
#define LAPIC_ERROR_IRQ LAPIC_IRQ(1)

#define APIC_ID 0x20
#define APIC_LVR 0x30
#define APIC_LVR_MASK 0xFF00FF
#define GET_APIC_VERSION(x) ((x)&0xFF)
#define GET_APIC_MAXLVT(x) (((x) >> 16) & 0xFF)
#define APIC_INTEGRATED(x) ((x)&0xF0)
#define APIC_XAPIC(x) ((x) >= 0x14)
#define APIC_LVR_EOI (1 << 24)
#define APIC_TASKPRI 0x80
#define APIC_TPRI_MASK 0xFF
#define APIC_ARBPRI 0x90
#define APIC_PROCPRI 0xA0
#define APIC_EOI 0xB0
#define APIC_RRR 0xC0
#define APIC_LDR 0xD0
#define APIC_DFR 0xE0
#define APIC_DFR_CLUSTER 0x0FFFFFFFul /* Clustered */
#define APIC_DFR_FLAT 0xFFFFFFFFul    /* Flat mode */
#define APIC_SVR 0xF0
#define APIC_SVR_FOCUS_DISABLED (1 << 9)
#define APIC_SVR_APIC_ENABLED (1 << 8)
#define APIC_ESR 0x280
#define APIC_TMR 0x180

#define APIC_ICR_LOW 0x300
#define APIC_DEST_SELF 0x40000
#define APIC_DEST_ALLINC 0x80000
#define APIC_DEST_ALLBUT 0xC0000
#define APIC_ICR_RR_MASK 0x30000
#define APIC_ICR_RR_INVALID 0x00000
#define APIC_ICR_RR_INPROG 0x10000
#define APIC_ICR_RR_VALID 0x20000
#define APIC_INT_LEVELTRIG 0x08000
#define APIC_INT_ASSERT 0x04000
#define APIC_ICR_BUSY 0x01000
#define APIC_DEST_LOGICAL 0x00800
#define APIC_DM_FIXED 0x00000
#define APIC_DM_LOWEST 0x00100
#define APIC_DM_SMI 0x00200
#define APIC_DM_REMRD 0x00300
#define APIC_DM_NMI 0x00400
#define APIC_DM_INIT 0x00500
#define APIC_DM_STARTUP 0x00600
#define APIC_DM_EXTINT 0x00700
#define APIC_VECTOR_MASK 0x000FF
#define APIC_ICR_HIGH 0x310
#define GET_APIC_DEST_FIELD(x) (((x) >> 24) & 0xFF)
#define SET_APIC_DEST_FIELD(x) ((x) << 24)

#define APIC_LVTT 0x320
#define APIC_LVTPC 0x340
#define APIC_LVT0 0x350
#define APIC_LVT_LEVEL_TRIGGER (1 << 15)
#define APIC_LVT_MASKED (1 << 16)
#define APIC_MODE_FIXED 0x000
#define APIC_MODE_NMI 0x400
#define APIC_MODE_EXTINT 0x700
#define APIC_LVT1 0x360
#define APIC_LVTERR 0x370
#define APIC_TMICT 0x380
#define APIC_TMCCT 0x390
#define APIC_TDCR 0x3E0
#define APIC_TDR_DIV_TMBASE (1 << 2)
#define APIC_TDR_DIV_1 0xB
#define APIC_TDR_DIV_2 0x0
#define APIC_TDR_DIV_4 0x1
#define APIC_TDR_DIV_8 0x2
#define APIC_TDR_DIV_16 0x3
#define APIC_TDR_DIV_32 0x8
#define APIC_TDR_DIV_64 0x9
#define APIC_TDR_DIV_128 0xA

#define APIC_BASE_MASK_MSR 0xFFFFF000

// IOAPIC definitions
#define APIC_IOREGSEL 0x0
#define APIC_IOWIN 0x10
#define IO_APIC_EOI 0x40
#define IO_APIC_ID 0x0
#define IO_APIC_VER 0x1
#define IO_APIC_TRIG_LEVEL 1
#define IO_APIC_TRIG_EDGE 0
#define IO_APIC_POL_LOW 1
#define IO_APIC_POL_HIGH 0

extern prtos_u32_t lapic_read(prtos_u32_t reg);
extern void lapic_write(prtos_u32_t reg, prtos_u32_t v);
extern struct io_apic_route_entry io_apic_read_entry(prtos_u8_t pin, prtos_s32_t io_apic);
extern void io_apic_write_entry(prtos_u8_t pin, struct io_apic_route_entry *e, prtos_s32_t io_apic);

enum io_apic_irq_destination_types {
    dest_fixed = 0,
    dest_lowest_prio = 1,
    dest_smi = 2,
    dest_reserved_1 = 3,
    dest_nmi = 4,
    dest_init = 5,
    dest_reserved_2 = 6,
    dest_ext_int = 7,
};

struct io_apic_route_entry {
    prtos_u32_t vector : 8, delivery_mode : 3,                   /* 000: FIXED
                                                                 * 001: lowest prio
                                                                 * 111: ExtINT
                                                                 */
        dest_mode : 1,                                           /* 0: physical, 1: logical */
        delivery_status : 1, polarity : 1, irr : 1, trigger : 1, /* 0: edge, 1: level */
        mask : 1,                                               /* 0: enabled, 1: disabled */
        __reserved2 : 15;

    union {
        struct {
            prtos_u32_t __reserved1 : 24, physical_dest : 4, __reserved2 : 4;
        } physical;

        struct {
            prtos_u32_t __reserved1 : 24, logical_dest : 8;
        } logical;
    } dest;

} __PACKED;

struct io_apic_versionsion_entry {
    union {
        struct {
            prtos_u32_t version : 8, __reserved2 : 7, PRQ : 1, entries : 8, __reserved1 : 8;
        };
        prtos_u32_t raw;
    };
};

#ifdef CONFIG_SMP
#define HALT_ALL_IPI_IRQ LAPIC_IRQ(2)
#define HALT_ALL_IPI_VECTOR (HALT_ALL_IPI_IRQ + FIRST_EXTERNAL_VECTOR)
#define SCHED_PENDING_IPI_IRQ LAPIC_IRQ(3)
#define SCHED_PENDING_IPI_VECTOR (SCHED_PENDING_IPI_IRQ + FIRST_EXTERNAL_VECTOR)

#define smp_halt_all() send_ipi(0, ALL_EXC_SELF, HALT_ALL_IPI_VECTOR)
#endif /* CONFIG_SMP */

#define NO_SHORTHAND_IPI 0x0
#define SELF_IPI 0x1
#define ALL_INC_SELF 0x2
#define ALL_EXC_SELF 0x3

extern void send_ipi(prtos_u8_t dst, prtos_u8_t dstShortHand, prtos_u8_t vector);

static inline prtos_s32_t lapic_get_max_lvt(void) {
    return GET_APIC_MAXLVT(lapic_read(APIC_LVR));
}

extern void setup_apic_common(void);

#endif
