/*
 * FILE: smp.h
 *
 * SMP related stuff
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_SMP_H_
#define _PRTOS_ARCH_SMP_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <arch/mpspec.h>
#include <arch/apic.h>
#include <linkage.h>

#ifdef CONFIG_APIC

#define BUS_HIGH_POLARITY 0
#define BUS_LOW_POLARITY 1
#define BUS_EDGE_TRIGGER 0
#define BUS_LEVEL_TRIGGER 1

struct x86_mp_conf {
    prtos_s32_t imcr;
    prtos_s32_t num_of_cpu;
    struct cpu_conf {
        prtos_u32_t rsv : 22, enabled : 1, bsp : 1, id : 8;
    } cpu[CONFIG_NO_CPUS];
    prtos_s32_t num_of_bus;
    struct bus_conf {
        prtos_u32_t id : 8, type : 8, polarity : 3, trigger_mode : 3;
    } bus[CONFIG_MAX_NO_BUSES];
    prtos_s32_t num_of_io_apic;
    struct io_apic_conf {
        prtos_u32_t id;
        prtos_address_t base_addr;
    } io_apic[CONFIG_MAX_NO_IOAPICS];
    prtos_s32_t num_of_io_int;
    struct io_int_conf {
        prtos_u64_t irq_type : 7,
        irq_over : 1,
        polarity : 3,
        trigger_mode : 3,
        src_bus_id : 8,
        src_bus_irq : 8,
        dst_io_apic_id : 8,
        dst_io_apic_irq : 8;
    } io_int[CONFIG_MAX_NO_IOINT];
    prtos_s32_t num_of_line_int;
    struct line_int_conf {
    } line_int[CONFIG_MAX_NO_LINT];
};

extern struct x86_mp_conf x86_mp_conf;

#endif /* CONFIG_APIC */

#endif
