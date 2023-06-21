/*
 * FILE: hwconf.h
 *
 * www.prtos.org
 *
 */
#ifndef _PRTOS_ARCH_HWCONF_H_
#define _PRTOS_ARCH_HWCONF_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

struct x86_lapic {
    prtos_u32_t rsv : 22, enabled : 1, bsp : 1, id : 8;
};

struct x86_io_apic {
    prtos_u32_t rsv : 23, enabled : 1, id : 8;
    prtos_u32_t baseAddr;
};

struct x86_io_apic_irq {
    prtos_u32_t rsv0 : 4, dstIoApic : 8, src_irq : 8, dst_irq : 8, polarity : 2, trigger_mode : 2;
    prtos_u32_t rsv1 : 16,
        type : 8,  // NMI, EXT, ...
        bus_id : 8;
};

struct x86_lapic_irq {
    prtos_u32_t rsv0 : 4,
        dst_lapic : 8,  // 0xFF: ALL
        src_irq : 8, dst_line_int : 8, polarity : 2, trigger_mode : 2;
    prtos_u32_t rsv1 : 16,
        type : 8,  // NMI, EXT, ...
        bus_id : 8;
};

enum bus_types {
    BUS_ISA,
    BUS_PCI,
};

struct x86_bus {
    prtos_u32_t rsv : 6, polarity : 1, trigger_mode : 1, type : 16, id : 8;
#define BUS_HIGH_POLARITY 0
#define BUS_LOW_POLARITY 1
#define BUS_EDGE_TRIGGER 0
#define BUS_LEVEL_TRIGGER 1
};

struct x86conf {
    prtos_u32_t rsv : 31, imcr : 1;
    prtos_u64_t lapic_base_addr;
    struct x86_lapic *lapic;
    struct x86_lapic_irq *lapic_irq;
    struct x86_io_apic *io_apic;
    struct x86_io_apic_irq *io_apic_irq;
    struct x86_bus *bus;
    prtos_u32_t lapic_len;
    prtos_u32_t lapic_irq_len;
    prtos_u32_t io_apic_len;
    prtos_u32_t io_apic_irq_len;
    prtos_u32_t bus_len;
};

extern struct x86_conf x86_conf_table;

#endif
