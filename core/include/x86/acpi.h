/*
 * FILE: acpi.h
 *
 * ACPI interface
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_ACPI_H_
#define _PRTOS_ARCH_ACPI_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <linkage.h>

struct acpi_header {
    prtos_s8_t signature[4];
    prtos_u32_t length;
    prtos_u8_t revision;
    prtos_u8_t checksum;
    prtos_s8_t oem_id[6];
    prtos_s8_t oem_table_id[8];
    prtos_u32_t oem_revision;
    prtos_s8_t creator_id[4];
    prtos_u32_t creator_revision;
};

struct acpi_rsdp {
    prtos_s8_t signature[8];
    prtos_u8_t checksum;
    prtos_s8_t oem_id[6];
    prtos_u8_t revision;
    prtos_u32_t rsdt_addr;
    prtos_u32_t length;
    prtos_u64_t xsdt_addr;
    prtos_u8_t ext_checksum;
    prtos_u8_t reserved[3];
};

struct acpi_rsdt {
    struct acpi_header header;
    prtos_u32_t entry[0];
} __PACKED;

struct acpi_madt {
    struct acpi_header header;
    prtos_address_t lapic_addr;
    prtos_u32_t flags;
} __PACKED;

struct acpi_madt_lapic {
    prtos_u8_t acpi_proc_id;
    prtos_u8_t apic_id;
    prtos_u32_t flags;
} __PACKED;

struct acpi_madt_io_apic {
    prtos_u8_t id;
    prtos_u8_t reserved;
    prtos_u32_t ioapic_addr;
    prtos_u32_t gs_irq_base;
} __PACKED;

struct acpi_madt_irq_src {
#define ACPI_POL_MASK 0xc000
#define ACPI_POL_HIGH 0x4000
#define ACPI_POL_LOW 0xc000
    prtos_u8_t bus;
    prtos_u8_t source;
    prtos_u32_t gs_irq;
    prtos_u16_t flags;
} __PACKED;

struct acpi_madt_nmi_src {
    prtos_u16_t flags;
    prtos_u32_t gs_irq;
} __PACKED;

struct acpi_madt_lapic_nmi_src {
    prtos_u8_t acpi_cpu_id;
    prtos_u16_t flags;
    prtos_u8_t lapic_line_int;
} __PACKED;

struct acpi_madt_lapic_addr_over {
    prtos_u16_t reserved;
    prtos_u64_t lapic_addr;
} __PACKED;

struct acpi_madt_source_apic {
    prtos_u8_t io_apic_id;
    prtos_u8_t reserved;
    prtos_u32_t gs_irq_base;
    prtos_u64_t ls_apic_addr;
} __PACKED;

struct acpi_madt_lsapic {
    prtos_u8_t acpi_cpu_id;
    prtos_u8_t ls_apic_id;
    prtos_u8_t ls_apic_eid;
    prtos_u8_t reserved[3];
    prtos_u32_t fkags;
    prtos_u32_t acpi_cpu_uid;
    char cpu_uid_str[0];
} __PACKED;

struct acpi_madt_pirq_src {
    prtos_u16_t flags;
    prtos_u8_t irq_type;
    prtos_u8_t cpu_id;
    prtos_u8_t cpu_eid;
    prtos_u8_t io_sapic_vector;
    prtos_u32_t gs_irq;
    prtos_u32_t pirq_src_flags;
} __PACKED;

struct acpi_madt_lx2apic {
    prtos_u8_t reserver[2];
    prtos_u32_t x2apic_id;
    prtos_u32_t flags;
    prtos_u32_t acpi_cpu_id;
} __PACKED;

struct acpi_madr_lx2apic_nmi {
    prtos_u16_t flags;
    prtos_u32_t acpi_cpu_uid;
    prtos_u8_t lx2apic_line_int;
    prtos_u8_t reserved[3];
} __PACKED;

#define ACPI_MADT_LAPIC 0
#define ACPI_MADT_IOAPIC 1
#define ACPI_MADT_IRQ_SRC 2
#define ACPI_MADT_NMI_SRC 3
#define ACPI_MADT_LAPIC_NMI 4
#define ACPI_MADT_LAPIC_ADDR_OVER 5
#define ACPI_MADT_IOSAPIC 6
#define ACPI_MADT_LSAPIC 7
#define ACPI_MADT_PIRQ_SRC 8
#define ACPI_MADT_LX2APIC 9
#define ACPI_MADT_LX2APIC_NMI 10
#define ACPI_MADT_MAX 11

struct acpi_madt_ics {
    prtos_u8_t type;
    prtos_u8_t length;
    union {
        struct acpi_madt_lapic lapic;
        struct acpi_madt_io_apic io_apic;
        struct acpi_madt_irq_src irq_src;
        struct acpi_madt_nmi_src nmi_src;
        struct acpi_madt_lapic_nmi_src lapic_nmi_src;
        struct acpi_madt_lapic_addr_over lapic_addr_over;
        struct acpi_madt_source_apic sapic;
        struct acpi_madt_lsapic lsapic;
        struct acpi_madt_pirq_src p_irq_src;
        struct acpi_madt_lx2apic lx2apic;
        struct acpi_madr_lx2apic_nmi lx2apic_nmi;
    };
} __PACKED;

#endif /*_PRTOS_ARCH_ACPI_H_*/
