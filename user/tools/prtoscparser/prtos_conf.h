/*
 * FILE: prtos_conf.h
 *
 * PRTOSC definitions
 *
 * www.prtos.org
 */

#ifndef _PRTOSCDEF_H_
#define _PRTOSCDEF_H_

#include <prtos_inc/prtosconf.h>

struct prtos_conf_hm_slot_line_number {
    int action;
    int log;
};

struct prtos_conf_trace_line_number {
    int dev;
};

struct prtos_conf_memory_region_line_number {
    int line;
    int start_addr;
    int size;
    int flags;
};

struct prtos_conf_memory_area_line_number {
    int name;
    int line;
    int start_addr;
    int mapped_at;
    int size;
    int flags;
    int partition_id;
};

struct prtos_conf_sched_cyclic_slot_line_number {
    int id;
    int partition_id;
    int vCpuId;
    int start_exec;
    int end_exec;
};

struct prtos_conf_part_line_number {
    int id;
    int name;
    int flags;
    int num_of_vcpus;
    //    prtos_s32_t num_of_physical_memory_areas;
    //    prtos_u32_t physical_memory_areas_offset;
    int console_device;
    //    struct prtos_conf_part_arch arch;
    //   prtos_u32_t comm_ports_offset;
    //   prtos_s32_t noPorts;
    struct prtos_conf_hm_slot_line_number hm_table[PRTOS_HM_MAX_EVENTS];
    //  prtos_u32_t io_ports_offset;
    //  prtos_s32_t num_of_io_ports;
    struct prtos_conf_trace_line_number trace;
};

struct prtos_conf_sched_cyclic_plan_line_number {
    int name;
    int major_frame;  // in useconds
    int ext_sync;
};

struct prtos_conf_line_number {
    int file_version;
    struct {
        struct {
            int id;
            int features;  // Enable/disable features
            int freq;      // KHz
            int plan;
            /*union {
                struct {
                    struct prtos_conf_sched_cyclic_plan_line_number {
                        int majorFrame; // in useconds
                        //int slotsOffset;
                    } planTab[CONFIG_MAX_CYCLIC_PLANS];
                } cyclic;
                } schedParams;*/
        } cpu_table[CONFIG_NO_CPUS];
        // struct prtos_conf_hm_slot_line_number hm_table[PRTOS_HM_MAX_EVENTS];
        int container_dev;
        int hm_device;
        int console_device;
        int node_id;
        int cov_dev;
        int hw_irq_table[CONFIG_NO_HWIRQS];
        struct prtos_conf_trace_line_number trace;
    } hpv;
    struct {
        // int console_device;
        int entry_point;
    } rsw;
};

struct prtos_conf_mem_block_line_number {
    int line;
    int start_addr;
    int size;
};

struct prtos_conf_comm_channel_line_number {
    int type;
    union {
        struct {
            int max_length;
            int max_num_of_msgs;
        } q;
        struct {
            int max_length;
        } s;
    };
    int valid_period;
};

struct prtos_conf_comm_port_code_line_number {
    int name;
    int direction;
    int type;
};

struct ipcPort {
    int channel;
    char *partition_name;
    char *portName;
    prtos_u32_t partition_id;
    prtos_dev_t devId;
    int direction;
};

struct ipc_port_line_number {
    int partition_name;
    int portName;
    int partition_id;
    int dev_id;
    int direction;
};

struct src_ipvi {
    int ipvi_id;
    int id;
    struct dst_ipvi {
        int id;
    } * dst;
    int num_of_dsts;
};

struct src_ipvi_line_number {
    int id;
    struct dst_ipvi_line_number {
        int id;
    } * dst;
};

extern char *ipvi_dst_table;

extern struct src_ipvi_line_number *src_ipvi_table_line_number;
extern struct src_ipvi *src_ipvi_table;
extern int num_of_src_ipvi;

extern struct prtos_conf prtos_conf;
extern struct prtos_conf_line_number prtos_conf_line_number;

extern struct prtos_conf_memory_region *prtos_conf_mem_reg_table;
extern struct prtos_conf_memory_region_line_number *prtos_conf_mem_reg_table_line_number;

extern struct prtos_conf_memory_area *prtos_conf_mem_area_table;
extern struct prtos_conf_memory_area_line_number *prtos_conf_mem_area_table_line_number;

extern struct prtos_conf_sched_cyclic_slot *prtos_conf_sched_cyclic_slot_table;
extern struct prtos_conf_sched_cyclic_slot_line_number *prtos_conf_sched_cyclic_slot_table_line_number;

extern struct prtos_conf_part *prtos_conf_part_table;
extern struct prtos_conf_part_line_number *prtos_conf_part_table_line_number;

extern struct prtos_conf_io_port *prtos_conf_io_port_table;
extern struct prtos_conf_io_port_code_line_number *prtos_conf_io_port_table_line_number;

extern struct prtos_conf_comm_port *prtos_conf_comm_port_table;
extern struct prtos_conf_comm_port_code_line_number *prtos_conf_comm_port_table_line_number;
extern struct prtos_conf_comm_channel *prtos_conf_comm_channel_table;
extern struct prtos_conf_comm_channel_line_number *prtos_conf_comm_channel_table_line_number;
extern struct ipcPort *ipc_port_table;
extern struct ipc_port_line_number *ipc_port_table_line_number;
extern int num_of_ipc_ports;
// extern struct hmSlot hmSlot;
extern struct prtos_conf_mem_block *prtos_conf_mem_block_table;
extern struct prtos_conf_mem_block_line_number *prtos_conf_mem_block_table_line_number;

extern struct prtos_conf_sched_cyclic_plan *prtos_conf_sched_cyclic_plan_table;
extern struct prtos_conf_sched_cyclic_plan_line_number *prtos_conf_sched_cyclic_plan_table_line_number;

extern char *str_tables;

#define C_PARTITION (prtos_conf.num_of_partitions - 1)
#define C_COMM_CHANNEL (prtos_conf.num_of_comm_channels - 1)
#define C_FPSCHED (prtos_conf.noFpEntries - 1)
#define C_HPV_PHYSMEMAREA (prtos_conf.hpv.num_of_physical_memory_areas - 1)
#define C_PART_PHYSMEMAREA (part_table[C_PARTITION].num_of_physical_memory_areas - 1)
#define C_RSW_PHYSMEMAREA (prtos_conf.rsw.num_of_physical_memory_areas - 1)
#define C_PART_VIRTMEMAREA (part_table[C_PARTITION].noVirtualMemoryAreas - 1)
#define C_PART_IOPORT (part_table[C_PARTITION].num_of_io_ports - 1)
#define C_PART_COMMPORT (part_table[C_PARTITION].noPorts - 1)
#define C_CPU (prtos_conf.hpv.num_of_cpus - 1)
#define C_REGION (prtos_conf.num_of_regions - 1)
#define C_PHYSMEMAREA (prtos_conf.num_of_physical_memory_areas - 1)
#define C_COMMPORT (prtos_conf.num_of_comm_ports - 1)
#define C_IOPORT (prtos_conf.num_of_io_ports - 1)
#define C_CYCLICSLOT (prtos_conf.num_of_sched_cyclic_slots - 1)
#define C_CYCLICPLAN (prtos_conf.num_of_sched_cyclic_plans - 1)
#define C_MEMORYBLOCK_DEV (prtos_conf.device_table.num_of_mem_blocks - 1)
#define C_IPCPORT (num_of_ipc_ports - 1)

extern prtos_u32_t add_string(char *s);
extern prtos_dev_t look_up_device(char *name, int line);
extern void register_device(char *name, prtos_dev_t id, int line);
extern int add_dev_name(char *name);
extern void link_channels_to_ports(void);
extern void link_devices(void);

extern void setup_default_hyp_hm_actions(struct prtos_conf_hm_slot *hm_table);
extern void setup_default_part_hm_actions(struct prtos_conf_hm_slot *hm_table);
extern void sort_phys_mem_areas(void);
extern void sort_mem_regions(void);
extern void process_ipvi_table(void);

#endif
