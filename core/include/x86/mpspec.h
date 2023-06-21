/*
 * FILE: mpspec.h
 *
 * Intel's MP specification
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_MPSPEC_H_
#define _PRTOS_ARCH_MPSPEC_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#define SMP_MAGIC_IDENT (('_' << 24) | ('P' << 16) | ('M' << 8) | '_')
#define MAX_MPC_ENTRY 1024
#define MAX_APICS 256

struct mp_floating_pointer {
    prtos_s8_t signature[4];
    prtos_u32_t physPtr;
    prtos_u8_t length;
    prtos_u8_t specification;
    prtos_u8_t checksum;
    prtos_u8_t feature1;
    prtos_u8_t feature2;
#define ICMR_FLAG (1 << 7)
    prtos_u8_t feature3;
    prtos_u8_t feature4;
    prtos_u8_t feature5;
} __PACKED;

struct mp_config_table {
    prtos_u32_t signature;
#define MPC_SIGNATURE (('P' << 24) | ('M' << 16) | ('C' << 8) | 'P')
    prtos_u16_t length;
    prtos_s8_t spec;
    prtos_s8_t checksum;
    prtos_s8_t oem[8];
    prtos_s8_t productId[12];
    prtos_u32_t oem_ptr;
    prtos_u16_t oem_size;
    prtos_u16_t oem_count;
    prtos_u32_t lapic;
    prtos_u32_t reserved;
} __PACKED;

struct mp_config_processor {
    prtos_u8_t type;
    prtos_u8_t apic_id;
    prtos_u8_t apic_version;
    prtos_u8_t cpu_flag;
#define CPU_ENABLED 1
#define CPU_BOOTPROCESSOR 2
    prtos_u32_t cpu_feature;
#define CPU_STEPPING_MASK 0x0F
#define CPU_MODEL_MASK 0xF0
#define CPU_FAMILY_MASK 0xF00
    prtos_u32_t feature_flag;
    prtos_u32_t reserved[2];
} __PACKED;

struct mp_config_bus {
    prtos_u8_t type;
    prtos_u8_t bus_id;
    prtos_u8_t bus_type[6];
} __PACKED;

// List of Bus Type string values, Intel MP Spec.
#define BUSTYPE_EISA "EISA"
#define BUSTYPE_ISA "ISA"
#define BUSTYPE_INTERN "INTERN" /* Internal BUS */
#define BUSTYPE_MCA "MCA"
#define BUSTYPE_VL "VL" /* Local bus */
#define BUSTYPE_PCI "PCI"
#define BUSTYPE_PCMCIA "PCMCIA"
#define BUSTYPE_CBUS "CBUS"
#define BUSTYPE_CBUSII "CBUSII"
#define BUSTYPE_FUTURE "FUTURE"
#define BUSTYPE_MBI "MBI"
#define BUSTYPE_MBII "MBII"
#define BUSTYPE_MPI "MPI"
#define BUSTYPE_MPSA "MPSA"
#define BUSTYPE_NUBUS "NUBUS"
#define BUSTYPE_TC "TC"
#define BUSTYPE_VME "VME"
#define BUSTYPE_XPRESS "XPRESS"

struct mp_config_io_apic {
    prtos_u8_t type;
    prtos_u8_t apic_id;
    prtos_u8_t apic_version;
    prtos_u8_t flags;
#define MPC_APIC_USABLE 0x01
    prtos_u32_t apic_addr;
} __PACKED;

struct mp_config_int_src {
    prtos_u8_t type;
    prtos_u8_t irq_type;
    prtos_u16_t irq_flag;
    prtos_u8_t src_bus;
    prtos_u8_t src_bus_irq;
    prtos_u8_t dst_apic;
    prtos_u8_t dst_irq;
} __PACKED;

#define MP_IRQDIR_DEFAULT 0
#define MP_IRQDIR_HIGH 1
#define MP_IRQDIR_LOW 3

struct mp_config_level_int_src {
    prtos_u8_t type;
    prtos_u8_t irq_type;
    prtos_u16_t irq_flag;
    prtos_u8_t src_bus_id;
    prtos_u8_t src_bus_irq;
    prtos_u8_t dest_apic;
#define MP_APIC_ALL 0xFF
    prtos_u8_t dest_apic_line_int;
} __PACKED;

struct mp_config_oem_table {
    prtos_s8_t oem_signature[4];
#define MPC_OEM_SIGNATURE "_OEM"
    prtos_u16_t oem_length;
    prtos_s8_t oem_rev;
    prtos_s8_t oem_checksum;
    prtos_s8_t mpc_oem[8];
} __PACKED;

/*
 *	Default configurations
 *
 *	1	2 CPU ISA 82489DX
 *	2	2 CPU EISA 82489DX neither IRQ 0 timer nor IRQ 13 DMA chaining
 *	3	2 CPU EISA 82489DX
 *	4	2 CPU MCA 82489DX
 *	5	2 CPU ISA+PCI
 *	6	2 CPU EISA+PCI
 *	7	2 CPU MCA+PCI
 */

enum mp_bus_type {
    MP_BUS_ISA = 1,
    MP_BUS_EISA,
    MP_BUS_PCI,
    MP_BUS_MCA,
};

enum mp_irq_source_types {
    mpINT = 0,
    mpNMI = 1,
    mpSMI = 2,
    mpExtINT = 3,
};

enum mp_config_entry_types {
    MP_PROCESSOR = 0,
    MP_BUS,
    MP_IOAPIC,
    MP_INTSRC,
    MP_LINTSRC,
    MP_MAX,
};

#endif
