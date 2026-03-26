/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * platform.h
 *
 * Hardware platform operations. Intended for use by domain-0 kernel.
 *
 * Copyright (c) 2002-2006, K Fraser
 */

#ifndef __PRTOS_PUBLIC_PLATFORM_H__
#define __PRTOS_PUBLIC_PLATFORM_H__

#include "public_prtos.h"

#define PRTOSPF_INTERFACE_VERSION 0x03000001

/*
 * Set clock such that it would read <secs,nsecs> after 00:00:00 UTC,
 * 1 January, 1970 if the current system time was <system_time>.
 */
#define PRTOSPF_settime32           17
struct prtospf_settime32 {
    /* IN variables. */
    uint32_t secs;
    uint32_t nsecs;
    uint64_t system_time;
};
typedef struct prtospf_settime32 prtospf_settime32_t;
#define PRTOSPF_settime64           62
struct prtospf_settime64 {
    /* IN variables. */
    uint64_t secs;
    uint32_t nsecs;
    uint32_t mbz;
    uint64_t system_time;
};
typedef struct prtospf_settime64 prtospf_settime64_t;
#if __PRTOS_INTERFACE_VERSION__ < 0x00040600
#define PRTOSPF_settime PRTOSPF_settime32
#define prtospf_settime prtospf_settime32
#else
#define PRTOSPF_settime PRTOSPF_settime64
#define prtospf_settime prtospf_settime64
#endif
typedef struct prtospf_settime prtospf_settime_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_settime_t);

/*
 * Request memory range (@mfn, @mfn+@nr_mfns-1) to have type @type.
 * On x86, @type is an architecture-defined MTRR memory type.
 * On success, returns the MTRR that was used (@reg) and a handle that can
 * be passed to PRTOSPF_DEL_MEMTYPE to accurately tear down the new setting.
 * (x86-specific).
 */
#define PRTOSPF_add_memtype         31
struct prtospf_add_memtype {
    /* IN variables. */
    prtos_pfn_t mfn;
    uint64_t nr_mfns;
    uint32_t type;
    /* OUT variables. */
    uint32_t handle;
    uint32_t reg;
};
typedef struct prtospf_add_memtype prtospf_add_memtype_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_add_memtype_t);

/*
 * Tear down an existing memory-range type. If @handle is remembered then it
 * should be passed in to accurately tear down the correct setting (in case
 * of overlapping memory regions with differing types). If it is not known
 * then @handle should be set to zero. In all cases @reg must be set.
 * (x86-specific).
 */
#define PRTOSPF_del_memtype         32
struct prtospf_del_memtype {
    /* IN variables. */
    uint32_t handle;
    uint32_t reg;
};
typedef struct prtospf_del_memtype prtospf_del_memtype_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_del_memtype_t);

/* Read current type of an MTRR (x86-specific). */
#define PRTOSPF_read_memtype        33
struct prtospf_read_memtype {
    /* IN variables. */
    uint32_t reg;
    /* OUT variables. */
    prtos_pfn_t mfn;
    uint64_t nr_mfns;
    uint32_t type;
};
typedef struct prtospf_read_memtype prtospf_read_memtype_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_read_memtype_t);

#define PRTOSPF_microcode_update    35
struct prtospf_microcode_update {
    /* IN variables. */
    PRTOS_GUEST_HANDLE(const_void) data;/* Pointer to microcode data */
    uint32_t length;                  /* Length of microcode data. */
};
typedef struct prtospf_microcode_update prtospf_microcode_update_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_microcode_update_t);

#define PRTOSPF_platform_quirk      39
#define QUIRK_NOIRQBALANCING      1 /* Do not restrict IO-APIC RTE targets */
#define QUIRK_IOAPIC_BAD_REGSEL   2 /* IO-APIC REGSEL forgets its value    */
#define QUIRK_IOAPIC_GOOD_REGSEL  3 /* IO-APIC REGSEL behaves properly     */
struct prtospf_platform_quirk {
    /* IN variables. */
    uint32_t quirk_id;
};
typedef struct prtospf_platform_quirk prtospf_platform_quirk_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_platform_quirk_t);

#define PRTOSPF_efi_runtime_call    49
#define PRTOS_EFI_get_time                      1
#define PRTOS_EFI_set_time                      2
#define PRTOS_EFI_get_wakeup_time               3
#define PRTOS_EFI_set_wakeup_time               4
#define PRTOS_EFI_get_next_high_monotonic_count 5
#define PRTOS_EFI_get_variable                  6
#define PRTOS_EFI_set_variable                  7
#define PRTOS_EFI_get_next_variable_name        8
#define PRTOS_EFI_query_variable_info           9
#define PRTOS_EFI_query_capsule_capabilities   10
#define PRTOS_EFI_update_capsule               11

struct prtospf_efi_time {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint32_t ns;
    int16_t tz;
    uint8_t daylight;
};

struct prtospf_efi_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

struct prtospf_efi_runtime_call {
    uint32_t function;
    /*
     * This field is generally used for per sub-function flags (defined
     * below), except for the PRTOS_EFI_get_next_high_monotonic_count case,
     * where it holds the single returned value.
     */
    uint32_t misc;
    prtos_ulong_t status;
    union {
#define PRTOS_EFI_GET_TIME_SET_CLEARS_NS 0x00000001
        struct {
            struct prtospf_efi_time time;
            uint32_t resolution;
            uint32_t accuracy;
        } get_time;

        struct prtospf_efi_time set_time;

#define PRTOS_EFI_GET_WAKEUP_TIME_ENABLED 0x00000001
#define PRTOS_EFI_GET_WAKEUP_TIME_PENDING 0x00000002
        struct prtospf_efi_time get_wakeup_time;

#define PRTOS_EFI_SET_WAKEUP_TIME_ENABLE      0x00000001
#define PRTOS_EFI_SET_WAKEUP_TIME_ENABLE_ONLY 0x00000002
        struct prtospf_efi_time set_wakeup_time;

#define PRTOS_EFI_VARIABLE_NON_VOLATILE       0x00000001
#define PRTOS_EFI_VARIABLE_BOOTSERVICE_ACCESS 0x00000002
#define PRTOS_EFI_VARIABLE_RUNTIME_ACCESS     0x00000004
        struct {
            PRTOS_GUEST_HANDLE(void) name;  /* UCS-2/UTF-16 string */
            prtos_ulong_t size;
            PRTOS_GUEST_HANDLE(void) data;
            struct prtospf_efi_guid vendor_guid;
        } get_variable, set_variable;

        struct {
            prtos_ulong_t size;
            PRTOS_GUEST_HANDLE(void) name;  /* UCS-2/UTF-16 string */
            struct prtospf_efi_guid vendor_guid;
        } get_next_variable_name;

#define PRTOS_EFI_VARINFO_BOOT_SNAPSHOT       0x00000001
        struct {
            uint32_t attr;
            uint64_t max_store_size;
            uint64_t remain_store_size;
            uint64_t max_size;
        } query_variable_info;

        struct {
            PRTOS_GUEST_HANDLE(void) capsule_header_array;
            prtos_ulong_t capsule_count;
            uint64_t max_capsule_size;
            uint32_t reset_type;
        } query_capsule_capabilities;

        struct {
            PRTOS_GUEST_HANDLE(void) capsule_header_array;
            prtos_ulong_t capsule_count;
            uint64_t sg_list; /* machine address */
        } update_capsule;
    } u;
};
typedef struct prtospf_efi_runtime_call prtospf_efi_runtime_call_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_efi_runtime_call_t);

#define PRTOSPF_firmware_info       50
#define PRTOS_FW_DISK_INFO          1 /* from int 13 AH=08/41/48 */
#define PRTOS_FW_DISK_MBR_SIGNATURE 2 /* from MBR offset 0x1b8 */
#define PRTOS_FW_VBEDDC_INFO        3 /* from int 10 AX=4f15 */
#define PRTOS_FW_EFI_INFO           4 /* from EFI */
#define  PRTOS_FW_EFI_VERSION        0
#define  PRTOS_FW_EFI_CONFIG_TABLE   1
#define  PRTOS_FW_EFI_VENDOR         2
#define  PRTOS_FW_EFI_MEM_INFO       3
#define  PRTOS_FW_EFI_RT_VERSION     4
#define  PRTOS_FW_EFI_PCI_ROM        5
#define  PRTOS_FW_EFI_APPLE_PROPERTIES 6
#define PRTOS_FW_KBD_SHIFT_FLAGS    5
struct prtospf_firmware_info {
    /* IN variables. */
    uint32_t type;
    uint32_t index;
    /* OUT variables. */
    union {
        struct {
            /* Int13, Fn48: Check Extensions Present. */
            uint8_t device;                   /* %dl: bios device number */
            uint8_t version;                  /* %ah: major version      */
            uint16_t interface_support;       /* %cx: support bitmap     */
            /* Int13, Fn08: Legacy Get Device Parameters. */
            uint16_t legacy_max_cylinder;     /* %cl[7:6]:%ch: max cyl # */
            uint8_t legacy_max_head;          /* %dh: max head #         */
            uint8_t legacy_sectors_per_track; /* %cl[5:0]: max sector #  */
            /* Int13, Fn41: Get Device Parameters (as filled into %ds:%esi). */
            /* NB. First uint16_t of buffer must be set to buffer size.      */
            PRTOS_GUEST_HANDLE(void) edd_params;
        } disk_info; /* PRTOS_FW_DISK_INFO */
        struct {
            uint8_t device;                   /* bios device number  */
            uint32_t mbr_signature;           /* offset 0x1b8 in mbr */
        } disk_mbr_signature; /* PRTOS_FW_DISK_MBR_SIGNATURE */
        struct {
            /* Int10, AX=4F15: Get EDID info. */
            uint8_t capabilities;
            uint8_t edid_transfer_time;
            /* must refer to 128-byte buffer */
            PRTOS_GUEST_HANDLE(uint8) edid;
        } vbeddc_info; /* PRTOS_FW_VBEDDC_INFO */
        union prtospf_efi_info {
            uint32_t version;
            struct {
                uint64_t addr;                /* EFI_CONFIGURATION_TABLE */
                uint32_t nent;
            } cfg;
            struct {
                uint32_t revision;
                uint32_t bufsz;               /* input, in bytes */
                PRTOS_GUEST_HANDLE(void) name;  /* UCS-2/UTF-16 string */
            } vendor;
            struct {
                uint64_t addr;
                uint64_t size;
                uint64_t attr;
                uint32_t type;
            } mem;
            struct {
                /* IN variables */
                uint16_t segment;
                uint8_t bus;
                uint8_t devfn;
                uint16_t vendor;
                uint16_t devid;
                /* OUT variables */
                uint64_t address;
                prtos_ulong_t size;
            } pci_rom;
            struct {
                /* OUT variables */
                uint64_t address;
                prtos_ulong_t size;
            } apple_properties;
        } efi_info; /* PRTOS_FW_EFI_INFO */

        /* Int16, Fn02: Get keyboard shift flags. */
        uint8_t kbd_shift_flags; /* PRTOS_FW_KBD_SHIFT_FLAGS */
    } u;
};
typedef struct prtospf_firmware_info prtospf_firmware_info_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_firmware_info_t);

#define PRTOSPF_enter_acpi_sleep    51
struct prtospf_enter_acpi_sleep {
    /* IN variables */
#if __PRTOS_INTERFACE_VERSION__ < 0x00040300
    uint16_t pm1a_cnt_val;      /* PM1a control value. */
    uint16_t pm1b_cnt_val;      /* PM1b control value. */
#else
    uint16_t val_a;             /* PM1a control / sleep type A. */
    uint16_t val_b;             /* PM1b control / sleep type B. */
#endif
    uint32_t sleep_state;       /* Which state to enter (Sn). */
#define PRTOSPF_ACPI_SLEEP_EXTENDED 0x00000001
    uint32_t flags;             /* PRTOSPF_ACPI_SLEEP_*. */
};
typedef struct prtospf_enter_acpi_sleep prtospf_enter_acpi_sleep_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_enter_acpi_sleep_t);

#define PRTOSPF_change_freq         52
struct prtospf_change_freq {
    /* IN variables */
    uint32_t flags; /* Must be zero. */
    uint32_t cpu;   /* Physical cpu. */
    uint64_t freq;  /* New frequency (Hz). */
};
typedef struct prtospf_change_freq prtospf_change_freq_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_change_freq_t);

/*
 * Get idle times (nanoseconds since boot) for physical CPUs specified in the
 * @cpumap_bitmap with range [0..@cpumap_nr_cpus-1]. The @idletime array is
 * indexed by CPU number; only entries with the corresponding @cpumap_bitmap
 * bit set are written to. On return, @cpumap_bitmap is modified so that any
 * non-existent CPUs are cleared. Such CPUs have their @idletime array entry
 * cleared.
 */
#define PRTOSPF_getidletime         53
struct prtospf_getidletime {
    /* IN/OUT variables */
    /* IN: CPUs to interrogate; OUT: subset of IN which are present */
    PRTOS_GUEST_HANDLE(uint8) cpumap_bitmap;
    /* IN variables */
    /* Size of cpumap bitmap. */
    uint32_t cpumap_nr_cpus;
    /* Must be indexable for every cpu in cpumap_bitmap. */
    PRTOS_GUEST_HANDLE(uint64) idletime;
    /* OUT variables */
    /* System time when the idletime snapshots were taken. */
    uint64_t now;
};
typedef struct prtospf_getidletime prtospf_getidletime_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_getidletime_t);

#define PRTOSPF_set_processor_pminfo      54

/* ability bits */
#define PRTOS_PROCESSOR_PM_CX	1
#define PRTOS_PROCESSOR_PM_PX	2
#define PRTOS_PROCESSOR_PM_TX	4

/* cmd type */
#define PRTOS_PM_CX   0
#define PRTOS_PM_PX   1
#define PRTOS_PM_TX   2
#define PRTOS_PM_PDC  3

/* Px sub info type */
#define PRTOS_PX_PCT   1
#define PRTOS_PX_PSS   2
#define PRTOS_PX_PPC   4
#define PRTOS_PX_PSD   8

struct prtos_power_register {
    uint32_t     space_id;
    uint32_t     bit_width;
    uint32_t     bit_offset;
    uint32_t     access_size;
    uint64_t     address;
};

struct prtos_processor_csd {
    uint32_t    domain;      /* domain number of one dependent group */
    uint32_t    coord_type;  /* coordination type */
    uint32_t    num;         /* number of processors in same domain */
};
typedef struct prtos_processor_csd prtos_processor_csd_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_processor_csd_t);

struct prtos_processor_cx {
    struct prtos_power_register  reg; /* GAS for Cx trigger register */
    uint8_t     type;     /* cstate value, c0: 0, c1: 1, ... */
    uint32_t    latency;  /* worst latency (ms) to enter/exit this cstate */
    uint32_t    power;    /* average power consumption(mW) */
    uint32_t    dpcnt;    /* number of dependency entries */
    PRTOS_GUEST_HANDLE(prtos_processor_csd_t) dp; /* NULL if no dependency */
};
typedef struct prtos_processor_cx prtos_processor_cx_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_processor_cx_t);

struct prtos_processor_flags {
    uint32_t bm_control:1;
    uint32_t bm_check:1;
    uint32_t has_cst:1;
    uint32_t power_setup_done:1;
    uint32_t bm_rld_set:1;
};

struct prtos_processor_power {
    uint32_t count;  /* number of C state entries in array below */
    struct prtos_processor_flags flags;  /* global flags of this processor */
    PRTOS_GUEST_HANDLE(prtos_processor_cx_t) states; /* supported c states */
};

struct prtos_pct_register {
    uint8_t  descriptor;
    uint16_t length;
    uint8_t  space_id;
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  reserved;
    uint64_t address;
};

struct prtos_processor_px {
    uint64_t core_frequency; /* megahertz */
    uint64_t power;      /* milliWatts */
    uint64_t transition_latency; /* microseconds */
    uint64_t bus_master_latency; /* microseconds */
    uint64_t control;        /* control value */
    uint64_t status;     /* success indicator */
};
typedef struct prtos_processor_px prtos_processor_px_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_processor_px_t);

struct prtos_psd_package {
    uint64_t num_entries;
    uint64_t revision;
    uint64_t domain;
    uint64_t coord_type;
    uint64_t num_processors;
};

struct prtos_processor_performance {
    uint32_t flags;     /* flag for Px sub info type */
    uint32_t platform_limit;  /* Platform limitation on freq usage */
    struct prtos_pct_register control_register;
    struct prtos_pct_register status_register;
    uint32_t state_count;     /* total available performance states */
    PRTOS_GUEST_HANDLE(prtos_processor_px_t) states;
    struct prtos_psd_package domain_info;
    /* Coordination type of this processor */
#define PRTOS_CPUPERF_SHARED_TYPE_HW   1 /* HW does needed coordination */
#define PRTOS_CPUPERF_SHARED_TYPE_ALL  2 /* All dependent CPUs should set freq */
#define PRTOS_CPUPERF_SHARED_TYPE_ANY  3 /* Freq can be set from any dependent CPU */
    uint32_t shared_type;
};
typedef struct prtos_processor_performance prtos_processor_performance_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_processor_performance_t);

struct prtospf_set_processor_pminfo {
    /* IN variables */
    uint32_t id;    /* ACPI CPU ID */
    uint32_t type;  /* {PRTOS_PM_CX, PRTOS_PM_PX} */
    union {
        struct prtos_processor_power          power;/* Cx: _CST/_CSD */
        struct prtos_processor_performance    perf; /* Px: _PPC/_PCT/_PSS/_PSD */
        PRTOS_GUEST_HANDLE(uint32)            pdc;  /* _PDC */
    } u;
};
typedef struct prtospf_set_processor_pminfo prtospf_set_processor_pminfo_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_set_processor_pminfo_t);

#define PRTOSPF_get_cpuinfo 55
struct prtospf_pcpuinfo {
    /* IN */
    uint32_t prtos_cpuid;
    /* OUT */
    /* The maxium cpu_id that is present */
    uint32_t max_present;
#define PRTOS_PCPU_FLAGS_ONLINE   1
    /* Correponding prtos_cpuid is not present*/
#define PRTOS_PCPU_FLAGS_INVALID  2
    uint32_t flags;
    uint32_t apic_id;
    uint32_t acpi_id;
};
typedef struct prtospf_pcpuinfo prtospf_pcpuinfo_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_pcpuinfo_t);

#define PRTOSPF_get_cpu_version 48
struct prtospf_pcpu_version {
    /* IN */
    uint32_t prtos_cpuid;
    /* OUT */
    /* The maxium cpu_id that is present */
    uint32_t max_present;
    char vendor_id[12];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
};
typedef struct prtospf_pcpu_version prtospf_pcpu_version_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_pcpu_version_t);

#define PRTOSPF_cpu_online    56
#define PRTOSPF_cpu_offline   57
struct prtospf_cpu_ol
{
    uint32_t cpuid;
};
typedef struct prtospf_cpu_ol prtospf_cpu_ol_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_cpu_ol_t);

#define PRTOSPF_cpu_hotadd    58
struct prtospf_cpu_hotadd
{
	uint32_t apic_id;
	uint32_t acpi_id;
	uint32_t pxm;
};
typedef struct prtospf_cpu_hotadd prtospf_cpu_hotadd_t;

#define PRTOSPF_mem_hotadd    59
struct prtospf_mem_hotadd
{
    uint64_t spfn;
    uint64_t epfn;
    uint32_t pxm;
    uint32_t flags;
};
typedef struct prtospf_mem_hotadd prtospf_mem_hotadd_t;

#define PRTOSPF_core_parking  60

#define PRTOS_CORE_PARKING_SET 1
#define PRTOS_CORE_PARKING_GET 2
struct prtospf_core_parking {
    /* IN variables */
    uint32_t type;
    /* IN variables:  set cpu nums expected to be idled */
    /* OUT variables: get cpu nums actually be idled */
    uint32_t idle_nums;
};
typedef struct prtospf_core_parking prtospf_core_parking_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_core_parking_t);

/*
 * Access generic platform resources(e.g., accessing MSR, port I/O, etc)
 * in unified way. Batch resource operations in one call are supported and
 * they are always non-preemptible and executed in their original order.
 * The batch itself returns a negative integer for general errors, or a
 * non-negative integer for the number of successful operations. For the latter
 * case, the @ret in the failed entry (if any) indicates the exact error.
 */
#define PRTOSPF_resource_op   61

#define PRTOS_RESOURCE_OP_MSR_READ  0
#define PRTOS_RESOURCE_OP_MSR_WRITE 1

/*
 * Specially handled MSRs:
 * - MSR_IA32_TSC
 * READ: Returns the scaled system time(ns) instead of raw timestamp. In
 *       multiple entry case, if other MSR read is followed by a MSR_IA32_TSC
 *       read, then both reads are guaranteed to be performed atomically (with
 *       IRQ disabled). The return time indicates the point of reading that MSR.
 * WRITE: Not supported.
 */

struct prtospf_resource_entry {
    union {
        uint32_t cmd;   /* IN: PRTOS_RESOURCE_OP_* */
        int32_t  ret;   /* OUT: return value for failed entry */
    } u;
    uint32_t rsvd;      /* IN: padding and must be zero */
    uint64_t idx;       /* IN: resource address to access */
    uint64_t val;       /* IN/OUT: resource value to set/get */
};
typedef struct prtospf_resource_entry prtospf_resource_entry_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_resource_entry_t);

struct prtospf_resource_op {
    uint32_t nr_entries;    /* number of resource entry */
    uint32_t cpu;           /* which cpu to run */
    PRTOS_GUEST_HANDLE(prtospf_resource_entry_t) entries;
};
typedef struct prtospf_resource_op prtospf_resource_op_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_resource_op_t);

#define PRTOSPF_get_symbol   63
struct prtospf_symdata {
    /* IN/OUT variables */
    uint32_t namelen; /* IN:  size of name buffer                       */
                      /* OUT: strlen(name) of hypervisor symbol (may be */
                      /*      larger than what's been copied to guest)  */
    uint32_t symnum;  /* IN:  Symbol to read                            */
                      /* OUT: Next available symbol. If same as IN then */
                      /*      we reached the end                        */

    /* OUT variables */
    PRTOS_GUEST_HANDLE(char) name;
    uint64_t address;
    char type;
};
typedef struct prtospf_symdata prtospf_symdata_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_symdata_t);

/*
 * Fetch the video console information and mode setup by PRTOS.  A non-
 * negative return value indicates the size of the (part of the) structure
 * which was filled.
 */
#define PRTOSPF_get_dom0_console 64
typedef struct dom0_vga_console_info prtospf_dom0_console_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_dom0_console_t);

#define PRTOSPF_get_ucode_revision 65
struct prtospf_ucode_revision {
    uint32_t cpu;             /* IN:  CPU number to get the revision from.  */
    uint32_t signature;       /* OUT: CPU signature (CPUID.1.EAX).          */
    uint32_t pf;              /* OUT: Platform Flags (Intel only)           */
    uint32_t revision;        /* OUT: Microcode Revision.                   */
};
typedef struct prtospf_ucode_revision prtospf_ucode_revision_t;
DEFINE_PRTOS_GUEST_HANDLE(prtospf_ucode_revision_t);

/*
 * ` enum neg_errnoval
 * ` HYPERVISOR_platform_op(const struct prtos_platform_op*);
 */
struct prtos_platform_op {
    uint32_t cmd;
    uint32_t interface_version; /* PRTOSPF_INTERFACE_VERSION */
    union {
        prtospf_settime_t               settime;
        prtospf_settime32_t             settime32;
        prtospf_settime64_t             settime64;
        prtospf_add_memtype_t           add_memtype;
        prtospf_del_memtype_t           del_memtype;
        prtospf_read_memtype_t          read_memtype;
        prtospf_microcode_update_t      microcode;
        prtospf_platform_quirk_t        platform_quirk;
        prtospf_efi_runtime_call_t      efi_runtime_call;
        prtospf_firmware_info_t         firmware_info;
        prtospf_enter_acpi_sleep_t      enter_acpi_sleep;
        prtospf_change_freq_t           change_freq;
        prtospf_getidletime_t           getidletime;
        prtospf_set_processor_pminfo_t  set_pminfo;
        prtospf_pcpuinfo_t              pcpu_info;
        prtospf_pcpu_version_t          pcpu_version;
        prtospf_cpu_ol_t                cpu_ol;
        prtospf_cpu_hotadd_t            cpu_add;
        prtospf_mem_hotadd_t            mem_add;
        prtospf_core_parking_t          core_parking;
        prtospf_resource_op_t           resource_op;
        prtospf_symdata_t               symdata;
        prtospf_dom0_console_t          dom0_console;
        prtospf_ucode_revision_t        ucode_revision;
        uint8_t                       pad[128];
    } u;
};
typedef struct prtos_platform_op prtos_platform_op_t;
DEFINE_PRTOS_GUEST_HANDLE(prtos_platform_op_t);

#endif /* __PRTOS_PUBLIC_PLATFORM_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
