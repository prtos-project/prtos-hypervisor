/*
 * FILE: prtosconf.h
 *
 * Config parameters for both, prtos and partitions
 *
 * www.prtos.org
 */

#ifndef _PRTOSCONF_H_
#define _PRTOSCONF_H_

#include __PRTOS_INCFLD(arch/prtosconf.h)
#include __PRTOS_INCFLD(devid.h)
#include __PRTOS_INCFLD(linkage.h)
#include __PRTOS_INCFLD(prtosef.h)

typedef struct {
    prtos_u32_t id : 16, sub_id : 16;
} prtos_dev_t;

struct prtos_conf_hm_slot {
    prtos_u32_t action : 31, log : 1;

// Logging
#define PRTOS_HM_LOG_DISABLED 0
#define PRTOS_HM_LOG_ENABLED 1

// HM Actions list
#define PRTOS_HM_AC_IGNORE 0
#define PRTOS_HM_AC_SHUTDOWN 1
#define PRTOS_HM_AC_PARTITION_COLD_RESET 2
#define PRTOS_HM_AC_PARTITION_WARM_RESET 3
#define PRTOS_HM_AC_HYPERVISOR_COLD_RESET 4
#define PRTOS_HM_AC_HYPERVISOR_WARM_RESET 5
#define PRTOS_HM_AC_SUSPEND 6
#define PRTOS_HM_AC_PARTITION_HALT 7
#define PRTOS_HM_AC_HYPERVISOR_HALT 8
#define PRTOS_HM_AC_PROPAGATE 9
#define PRTOS_HM_AC_SWITCH_TO_MAINTENANCE 10
#define PRTOS_HM_MAX_ACTIONS 11
};

// HM events prtoss triggered
// Raised by PRTOS when an internal grave error is detected
#define PRTOS_HM_EV_INTERNAL_ERROR 0  // affects PRTOS

// Raised by PRTOS when an internal error is detected but the system can be recovered
#define PRTOS_HM_EV_SYSTEM_ERROR 1

// Raised by the partition (through the traces)
#define PRTOS_HM_EV_PARTITION_ERROR 2

// Raised by the timer
#define PRTOS_HM_EV_WATCHDOG_TIMER 3

// Raised when a partition uses the FP without having permissions
#define PRTOS_HM_EV_FP_ERROR 4

// Try to access to the PRTOS's memory space
#define PRTOS_HM_EV_MEM_PROTECTION 5

// Unexpected trap raised
#define PRTOS_HM_EV_UNEXPECTED_TRAP 6

#define PRTOS_HM_MAX_GENERIC_EVENTS 7

struct prtos_conf_comm_port {
    prtos_u32_t name_offset;
    prtos_s32_t channel_id;
#define PRTOS_NULL_CHANNEL -1
    prtos_s32_t direction;
#define PRTOS_SOURCE_PORT 0x2
#define PRTOS_DESTINATION_PORT 0x1
    prtos_s32_t type;
#define PRTOS_SAMPLING_PORT 0
#define PRTOS_QUEUING_PORT 1
};

// sched-cyclic-slot
struct prtos_conf_sched_cyclic_slot {
    prtos_id_t id;
    prtos_id_t partition_id;
    prtos_id_t vcpu_id;
    prtos_u32_t start_exec;  // offset (usec)
    prtos_u32_t end_exec;    // offset+duration (usec)
};

struct prtos_conf_sched_cyclic_plan {
    prtos_u32_t name_offset;
    prtos_id_t id;
    prtos_u32_t major_frame;  // in useconds
    prtos_s32_t num_of_slotss;
#ifdef CONFIG_PLAN_EXTSYNC
    prtos_s32_t ext_sync;  // -1 means no sync
#endif
    prtos_u32_t slots_offset;
};

struct prtos_conf_memory_area {
    prtos_u32_t name_offset;
    prtos_address_t start_addr;
    prtos_address_t mapped_at;
    prtos_u_size_t size;
#define PRTOS_MEM_AREA_SHARED (1 << 0)
#define PRTOS_MEM_AREA_UNMAPPED (1 << 1)
#define PRTOS_MEM_AREA_READONLY (1 << 2)
#define PRTOS_MEM_AREA_UNCACHEABLE (1 << 3)
#define PRTOS_MEM_AREA_ROM (1 << 4)
#define PRTOS_MEM_AREA_FLAG0 (1 << 5)
#define PRTOS_MEM_AREA_FLAG1 (1 << 6)
#define PRTOS_MEM_AREA_FLAG2 (1 << 7)
#define PRTOS_MEM_AREA_FLAG3 (1 << 8)
#define PRTOS_MEM_AREA_TAGGED (1 << 9)
#define PRTOS_MEM_AREA_IOMMU (1 << 10)
    prtos_u32_t flags;
    prtos_u32_t memory_region_offset;
};

struct prtos_conf_rsw {
    prtos_s32_t num_of_physical_memory_areas;
    prtos_u32_t physical_memory_areas_offset;
};

struct prtos_conf_trace {
    prtos_dev_t dev;
    prtos_u32_t bitmap;
};

struct prtos_conf_part {
    prtos_id_t id;
    prtos_u32_t name_offset;
    prtos_u32_t flags;
#define PRTOS_PART_SYSTEM 0x100
#define PRTOS_PART_FP 0x200
    prtos_u32_t num_of_vcpus;
    prtos_u32_t hw_irqs;
    prtos_s32_t num_of_physical_memory_areas;
    prtos_u32_t physical_memory_areas_offset;
    prtos_dev_t console_device;
    struct prtos_conf_part_arch arch;
    prtos_u32_t comm_ports_offset;
    prtos_s32_t noPorts;
    struct prtos_conf_hm_slot hm_table[PRTOS_HM_MAX_EVENTS];
    prtos_u32_t io_ports_offset;
    prtos_s32_t num_of_io_ports;
    struct prtos_conf_trace trace;
    struct prtos_conf_part_ipvi {
        prtos_u32_t dst_offset;
        prtos_s32_t num_of_dsts;
    } ipvi_table[CONFIG_PRTOS_MAX_IPVI];
};

struct prtos_conf_comm_channel {
#define PRTOS_SAMPLING_CHANNEL 0
#define PRTOS_QUEUING_CHANNEL 1
    prtos_s32_t type;

    union {
        struct {
            prtos_s32_t max_length;
            prtos_s32_t max_num_of_msgs;
        } q;
        struct {
            prtos_s32_t max_length;
            prtos_u32_t valid_period;
            prtos_s32_t num_of_receivers;
        } s;
    };
};

struct prtos_conf_memory_region {
    prtos_address_t start_addr;
    prtos_u_size_t size;
#define PRTOSC_REG_FLAG_PGTAB (1 << 0)
#define PRTOSC_REG_FLAG_ROM (1 << 1)
    prtos_u32_t flags;
};

struct prtos_conf_hw_irq {
    prtos_s32_t owner;
#define PRTOS_IRQ_NO_OWNER -1
};

struct prtos_conf_hyp {
    prtos_s32_t num_of_physical_memory_areas;
    prtos_u32_t physical_memory_areas_offset;
    prtos_s32_t num_of_cpus;
    struct _cpu {
        prtos_id_t id;
        prtos_u32_t features;  // Enable/disable features
        prtos_u32_t freq;      // KHz
#define PRTOS_CPUFREQ_AUTO 0

#define CYCLIC_SCHED 0
#define FP_SCHED 1
        prtos_u32_t sched_policy;
        prtos_u32_t sched_cyclic_plans_offset;
        prtos_s32_t num_of_sched_cyclic_plans;
    } cpu_table[CONFIG_NO_CPUS];
    struct prtos_conf_hyp_arch arch;
    struct prtos_conf_hm_slot hm_table[PRTOS_HM_MAX_EVENTS];
    prtos_dev_t hm_device;
    prtos_dev_t console_device;
    prtos_id_t node_id;
    struct prtos_conf_hw_irq hw_irq_table[CONFIG_NO_HWIRQS];
    struct prtos_conf_trace trace;
};

#if defined(CONFIG_DEV_MEMBLOCK) || defined(CONFIG_DEV_MEMBLOCK_MODULE)
struct prtos_conf_mem_block {
    prtos_u32_t physical_memory_areas_offset;
};
#endif

struct prtos_conf_device {
#if defined(CONFIG_DEV_MEMBLOCK) || defined(CONFIG_DEV_MEMBLOCK_MODULE)
    prtos_address_t mem_blocks_offset;
    prtos_s32_t num_of_mem_blocks;
#endif
#if defined(CONFIG_DEV_UART) || defined(CONFIG_DEV_UART_MODULE)
    struct prtos_conf_uart_cfg {
        prtos_u32_t baud_rate;
    } uart[CONFIG_DEV_NO_UARTS];
#endif
#ifdef CONFIG_DEV_VGA
    struct prtos_conf_vga_cfg {
    } vga;
#endif
};

struct prtos_conf_rsv_mem {
    void *obj;
    prtos_u32_t used_align;
#define RSV_MEM_USED 0x80000000
    prtos_u32_t size;
} __PACKED;

struct prtos_conf_boot_part {
#define PRTOS_PART_BOOT 0x1
    prtos_u32_t flags;
    prtos_address_t hdr_phys_addr;
    prtos_address_t entry_point;
    prtos_address_t image_start;
    prtos_u_size_t img_size;
    prtos_u32_t num_of_custom_files;
    struct xef_custom_file custom_file_table[CONFIG_MAX_NO_CUSTOMFILES];
};

struct prtos_conf_rsw_info {
    prtos_address_t entry_point;
};

struct prtos_conf_vcpu {
    prtos_id_t cpu;
};

#define PRTOSC_VERSION 1
#define PRTOSC_SUBVERSION 0
#define PRTOSC_REVISION 0

struct prtos_conf {
#define PRTOSC_SIGNATURE 0x24584d43  // $PRTOSC
    prtos_u32_t signature;
    prtos_u8_t digest[PRTOS_DIGEST_BYTES];
    prtos_u_size_t dataSize;
    prtos_u_size_t size;
// Reserved(8).VERSION(8).SUBVERSION(8).REVISION(8)
#define PRTOSC_SET_VERSION(_ver, _subver, _rev) ((((_ver)&0xFF) << 16) | (((_subver)&0xFF) << 8) | ((_rev)&0xFF))
#define PRTOSC_GET_VERSION(_v) (((_v) >> 16) & 0xFF)
#define PRTOSC_GET_SUBVERSION(_v) (((_v) >> 8) & 0xFF)
#define PRTOSC_GET_REVISION(_v) ((_v)&0xFF)
    prtos_u32_t version;
    prtos_u32_t file_version;
    prtos_address_t rsv_mem_tab_offset;
    prtos_address_t name_offset;
    struct prtos_conf_hyp hpv;
    struct prtos_conf_rsw rsw;
    prtos_address_t part_table_offset;
    prtos_s32_t num_of_partitions;
    prtos_address_t boot_part_table_offset;
    prtos_address_t rsw_info_offset;
    prtos_address_t memory_regions_offset;
    prtos_u32_t num_of_regions;
    prtos_address_t sched_cyclic_slots_offset;
    prtos_s32_t num_of_sched_cyclic_slots;
    prtos_address_t sched_cyclic_plans_offset;
    prtos_s32_t num_of_sched_cyclic_plans;
    prtos_address_t comm_channel_table_offset;
    prtos_s32_t num_of_comm_channels;
    prtos_address_t physical_memory_areas_offset;
    prtos_s32_t num_of_physical_memory_areas;
    prtos_address_t comm_ports_offset;
    prtos_s32_t num_of_comm_ports;
    prtos_address_t io_ports_offset;
    prtos_s32_t num_of_io_ports;
    prtos_address_t ipvi_dst_offset;
    prtos_s32_t num_of_ipvi_dsts;

    prtos_address_t vcpu_table_offset;
    prtos_address_t strings_offset;
    prtos_s32_t string_table_length;
    struct prtos_conf_device device_table;
} __PACKED;

#ifdef _PRTOS_KERNEL_
extern const struct prtos_conf prtos_conf_table;
extern struct prtos_conf_part *prtos_conf_part_table;
extern struct prtos_conf_boot_part *prtos_conf_boot_part_table;
extern struct prtos_conf_rsw_info *prtos_conf_rsw_info;
extern struct prtos_conf_memory_region *prtos_conf_mem_reg_table;
extern struct prtos_conf_comm_channel *prtos_conf_comm_channel_table;
extern struct prtos_conf_memory_area *prtos_conf_phys_mem_area_table;
extern struct prtos_conf_comm_port *prtos_conf_comm_ports;
extern struct prtos_conf_io_port *prtos_conf_io_port_table;
extern struct prtos_conf_sched_cyclic_slot *prtos_conf_sched_cyclic_slot_table;
extern struct prtos_conf_sched_cyclic_plan *prtos_conf_sched_cyclic_plan_table;
extern prtos_u8_t *prtos_conf_dst_ipvi;
extern prtos_s8_t *prtos_conf_string_tab;

extern struct prtos_conf_vcpu *prtos_conf_vcpu_table;
extern struct prtos_conf_rsv_mem *prtos_conf_rsv_mem_table;
#if defined(CONFIG_DEV_MEMBLOCK) || defined(CONFIG_DEV_MEMBLOCK_MODULE)
extern struct prtos_conf_mem_block *prtos_conf_mem_block_table;
#endif

#endif

#endif
