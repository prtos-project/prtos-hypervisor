/*
 * FILE: setup.c
 *
 * Setting up and starting up the kernel
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <comp.h>
#include <rsvmem.h>
#include <kdevice.h>
#include <ktimer.h>
#include <stdc.h>
#include <irqs.h>
#include <objdir.h>
#include <physmm.h>
#include <processor.h>
#include <sched.h>
#include <spinlock.h>
#include <smp.h>
#include <kthread.h>
#include <vmmap.h>
#include <virtmm.h>
#include <prtosconf.h>
#include <local.h>

#include <objects/console.h>
#include <arch/paging.h>
#include <arch/prtos_def.h>

// CPU's frequency
prtos_u32_t cpu_khz;
prtos_u16_t __nr_cpus = 0;

struct prtos_conf_part *prtos_conf_partition_table;
struct prtos_conf_memory_region *prtos_conf_mem_reg_table;
struct prtos_conf_memory_area *prtos_conf_phys_mem_area_table;
struct prtos_conf_comm_channel *prtos_conf_comm_channel_table;
struct prtos_conf_comm_port *prtos_conf_comm_ports;
struct prtos_conf_io_port *prtos_conf_io_port_table;
struct prtos_conf_sched_cyclic_slot *prtos_conf_sched_cyclic_slot_table;
struct prtos_conf_sched_cyclic_plan *prtos_conf_sched_cyclic_plan_table;
struct prtos_conf_rsv_mem *prtos_conf_rsv_mem_table;
struct prtos_conf_boot_part *prtos_conf_boot_partition_table;
struct prtos_conf_rsw_info *prtos_conf_rsw_info;
prtos_u8_t *prtos_conf_dst_ipvi;
prtos_s8_t *prtos_conf_string_tab;

struct prtos_conf_vcpu *prtos_conf_vcpu_table;

#if defined(CONFIG_DEV_MEMBLOCK) || defined(CONFIG_DEV_MEMBLOCK_MODULE)
struct prtos_conf_mem_block *prtos_conf_mem_block_table;
#endif

// Local process info
local_processor_t local_processor_info[CONFIG_NO_CPUS];

extern prtos_u32_t reset_status_init[];

barrier_t smp_start_barrier = BARRIER_INIT;
barrier_t smp_boot_barrier = BARRIER_INIT;
barrier_mask_t smp_barrier_mask = BARRIER_MASK_INIT;

extern const prtos_u8_t _sprtos[], _eprtos[], phys_prtos_conf_table[];
extern void start(void);

struct prtos_hdr prtos_hdr __PRTOS_HDR = {
    .start_signature = PRTOS_EXEC_HYP_MAGIC,
    .compilation_prtos_abi_version = PRTOS_SET_VERSION(PRTOS_ABI_VERSION, PRTOS_ABI_SUBVERSION, PRTOS_ABI_REVISION),
    .compilation_prtos_api_version = PRTOS_SET_VERSION(PRTOS_API_VERSION, PRTOS_API_SUBVERSION, PRTOS_API_REVISION),
    .num_of_custom_files = 1,
    .custom_file_table =
        {
            [0] =
                {
                    .start_addr = (prtos_address_t)phys_prtos_conf_table,
                    .size = 0,
                },
        },
    .end_signature = PRTOS_EXEC_HYP_MAGIC,
};

extern __NOINLINE void free_boot_mem(void);

void idle_task(void) {
    while (1) {
        do_preemption();
    }
}

void halt_system(void) {
    extern void __halt_system(void);
#ifdef CONFIG_SMP
    if (GET_CPU_ID() == 0)
#endif
#ifdef CONFIG_DEBUG
        kprintf("System halted.\n");
#endif
#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_HYP_HALT, 0, 0);
#endif
#ifdef CONFIG_SMP
    smp_halt_all();
#endif
    __halt_system();
}

void reset_system(prtos_u32_t reset_mode) {
    extern prtos_u32_t sys_reset_counter[];
    extern void start(void);
    extern void _reset(prtos_address_t);
    cpu_ctxt_t ctxt;

    ASSERT(!hw_is_sti());
    sys_reset_counter[0]++;
#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_HYP_RESET, 1, (prtos_word_t *)&reset_mode);
#endif
    if ((reset_mode & PRTOS_RESET_MODE) == PRTOS_WARM_RESET) {
#ifndef CONFIG_AARCH64  // FIXME: this just a WA for aarch64
        _reset((prtos_address_t)start);
#endif
    } else {  // Cold reset
        sys_reset_counter[0] = 0;
#ifndef CONFIG_AARCH64  // FIXME: this just a WA for aarch64
        _reset((prtos_address_t)start);
#endif
    }
    get_cpu_ctxt(&ctxt);
    system_panic(&ctxt, "Unreachable point\n");
}

static void __VBOOT create_local_info(void) {
    prtos_s32_t e, e1;
    if (!GET_NRCPUS()) {
        cpu_ctxt_t ctxt;
        get_cpu_ctxt(&ctxt);
        system_panic(&ctxt, "No cpu found in the system\n");
    }
    memset(local_processor_info, 0, sizeof(local_processor_t) * CONFIG_NO_CPUS);
    for (e = 0; e < CONFIG_NO_CPUS; e++)
        for (e1 = 0; e1 < HWIRQS_VECTOR_SIZE; e1++) local_processor_info[e].cpu.global_irq_mask[e1] = 0xFFFFFFFF;
}

static void __VBOOT local_setup(prtos_s32_t cpu_id, kthread_t *idle) {
    ASSERT(!hw_is_sti());
    ASSERT(prtos_conf_table.hpv.num_of_cpus > cpu_id);
    setup_cpu();
    setup_arch_local(cpu_id);
    setup_hw_timer();
    setup_ktimers();
    init_sched_local(idle);
#ifdef CONFIG_SMP
    barrier_write_mask(&smp_barrier_mask);
#endif
}

static void __VBOOT setup_partitions(void) {
    prtos_address_t st, end, v_start, v_end;
    partition_t *p;
    prtos_s32_t e, a;
    kprintf("%d Partition(s) created\n", prtos_conf_table.num_of_partitions);

    // Creating the partitions
    for (e = 0; e < prtos_conf_table.num_of_partitions; e++) {
        if ((p = create_partition(&prtos_conf_partition_table[e]))) {
            kprintf("P%d (\"%s\":%d:%d) flags: [", e, &prtos_conf_string_tab[prtos_conf_partition_table[e].name_offset], prtos_conf_partition_table[e].id,
                    prtos_conf_partition_table[e].num_of_vcpus);
            if (prtos_conf_partition_table[e].flags & PRTOS_PART_SYSTEM) kprintf(" SYSTEM");
            if (prtos_conf_partition_table[e].flags & PRTOS_PART_FP) kprintf(" FP");
            kprintf(" ]:\n");
            for (a = 0; a < prtos_conf_partition_table[e].num_of_physical_memory_areas; a++) {
                st = prtos_conf_phys_mem_area_table[a + prtos_conf_partition_table[e].physical_memory_areas_offset].start_addr;
                end = st + prtos_conf_phys_mem_area_table[a + prtos_conf_partition_table[e].physical_memory_areas_offset].size - 1;
                v_start = prtos_conf_phys_mem_area_table[a + prtos_conf_partition_table[e].physical_memory_areas_offset].mapped_at;
                v_end = v_start + prtos_conf_phys_mem_area_table[a + prtos_conf_partition_table[e].physical_memory_areas_offset].size - 1;

                kprintf("    [0x%lx:0x%lx - 0x%lx:0x%lx]", st, v_start, end, v_end);
                kprintf(" flags: 0x%llx", prtos_conf_phys_mem_area_table[a + prtos_conf_partition_table[e].physical_memory_areas_offset].flags);
                kprintf("\n");
            }

            if (prtos_conf_boot_partition_table[e].flags & PRTOS_PART_BOOT) {
                if (reset_partition(p, PRTOS_COLD_RESET, reset_status_init[0]) < 0) kprintf("Unable to reset partition %d\n", p->cfg->id);
                p->op_mode = PRTOS_OPMODE_IDLE;
            }

        } else {
            cpu_ctxt_t ctxt;
            get_cpu_ctxt(&ctxt);
            system_panic(&ctxt, "[LoadGuests] Error creating partition");
        }
    }
}

static void __VBOOT load_conf_table(void) {
    // Check configuration file
    if (prtos_conf_table.signature != PRTOSC_SIGNATURE) halt_system();
#define CALC_ABS_ADDR_PRTOSC(_offset) (void *)(prtos_conf_table._offset + (prtos_address_t) & prtos_conf_table)

    prtos_conf_partition_table = CALC_ABS_ADDR_PRTOSC(partition_table_offset);
    prtos_conf_boot_partition_table = CALC_ABS_ADDR_PRTOSC(boot_partition_table_offset);
    prtos_conf_rsw_info = CALC_ABS_ADDR_PRTOSC(rsw_info_offset);
    prtos_conf_mem_reg_table = CALC_ABS_ADDR_PRTOSC(memory_regions_offset);
    prtos_conf_phys_mem_area_table = CALC_ABS_ADDR_PRTOSC(physical_memory_areas_offset);
    prtos_conf_comm_channel_table = CALC_ABS_ADDR_PRTOSC(comm_channel_table_offset);
    prtos_conf_comm_ports = CALC_ABS_ADDR_PRTOSC(comm_ports_offset);
    prtos_conf_io_port_table = CALC_ABS_ADDR_PRTOSC(io_ports_offset);
    prtos_conf_sched_cyclic_slot_table = CALC_ABS_ADDR_PRTOSC(sched_cyclic_slots_offset);
    prtos_conf_sched_cyclic_plan_table = CALC_ABS_ADDR_PRTOSC(sched_cyclic_plans_offset);
    prtos_conf_string_tab = CALC_ABS_ADDR_PRTOSC(strings_offset);
    prtos_conf_rsv_mem_table = CALC_ABS_ADDR_PRTOSC(rsv_mem_tab_offset);
    prtos_conf_dst_ipvi = CALC_ABS_ADDR_PRTOSC(ipvi_dst_offset);
    prtos_conf_vcpu_table = CALC_ABS_ADDR_PRTOSC(vcpu_table_offset);

#if defined(CONFIG_DEV_MEMBLOCK) || defined(CONFIG_DEV_MEMBLOCK_MODULE)
    prtos_conf_mem_block_table = CALC_ABS_ADDR_PRTOSC(device_table.mem_blocks_offset);
#endif
}

#if defined(CONFIG_AARCH64)
#include <arch/layout.h>
__attribute__((aligned(SZ_4K))) char _kernel_sp_stack[SZ_4K * NCPU] = {0};

#define __section(s) __attribute__((__section__(s)))
#define __aligned(a) __attribute__((__aligned__(a)))

#define DEFINE_BOOT_PAGE_TABLES(name, nr) prtos_word_t __aligned(4096L) __section(".data.page_aligned") name[512 * (nr)]

#define DEFINE_BOOT_PAGE_TABLE(name) DEFINE_BOOT_PAGE_TABLES(name, 1)
#define DEFINE_BOOT_PAGE_TABLES(name, nr) prtos_word_t __aligned(4096L) __section(".data.page_aligned") name[512 * (nr)]

// DEFINE_BOOT_PAGE_TABLE(boot_pgtable);
// DEFINE_BOOT_PAGE_TABLE(boot_first);
// DEFINE_BOOT_PAGE_TABLE(boot_first_id);
// DEFINE_BOOT_PAGE_TABLE(boot_second_id);
// DEFINE_BOOT_PAGE_TABLE(boot_third_id);
// DEFINE_BOOT_PAGE_TABLE(boot_second);
// DEFINE_BOOT_PAGE_TABLES(boot_third, 4);

#if 0
prtos_word_t __attribute__((aligned(SZ_4K))) __section(".data.page_aligned") boot_pgtable[512];
prtos_word_t __attribute__((aligned(SZ_4K))) __section(".data.page_aligned") boot_first[512];
prtos_word_t __attribute__((aligned(SZ_4K))) __section(".data.page_aligned") boot_first_id[512];
prtos_word_t __attribute__((aligned(SZ_4K))) __section(".data.page_aligned") boot_second_id[512];
prtos_word_t __attribute__((aligned(SZ_4K))) __section(".data.page_aligned") boot_third_id[512];
prtos_word_t __attribute__((aligned(SZ_4K))) __section(".data.page_aligned") boot_second[512];
prtos_word_t __attribute__((aligned(SZ_4K))) __section(".data.page_aligned") boot_third[512 * 4];
prtos_word_t __attribute__((aligned(SZ_4K))) __section(".data.page_aligned") xen_fixmap[512];
#else

#endif

struct init_info {
    /* Pointer to the stack, used by head.S when entering in C */
    unsigned char *stack;
    /* Logical CPU ID, used by start_secondary */
    unsigned int cpuid;
};

// /* Xen stack for bringing up the first CPU. */
// static unsigned char __initdata cpu0_boot_stack[STACK_SIZE]
//        __attribute__((__aligned__(STACK_SIZE)));

__attribute__((aligned(CONFIG_KSTACK_SIZE))) char cpu0_boot_stack_prtos[CONFIG_KSTACK_SIZE] = {0};

/* Boot cpu data */
struct init_info init_data_prtos = {
    .stack = cpu0_boot_stack_prtos,
};

#endif

int prtos_kernel_run = 1;

void __VBOOT setup_kernel(prtos_s32_t cpu_id, kthread_t *idle) {
    ASSERT(!hw_is_sti());
    ASSERT(GET_CPU_ID() == 0);

#ifdef CONFIG_EARLY_OUTPUT
    extern void setup_early_output(void);
    setup_early_output();
#endif

    load_conf_table();

#if defined(CONFIG_AARCH64)  // Here added print code just for debug in the future.
    eprintf("cpu_id: %d\n", cpu_id);
    eprintf("idle: 0x%llx\n", idle);
    eprintf("base addr of cpu0_boot_stack_prtos: 0x%llx\n", &cpu0_boot_stack_prtos[0]);

    eprintf("_sprtos: 0x%llx --> pa:0x%llx\n", _sprtos, _sprtos - CONFIG_PRTOS_OFFSET + CONFIG_PRTOS_LOAD_ADDR);
    eprintf("_eprtos: 0x%llx --> pa:0x%llx\n", _eprtos, _eprtos - CONFIG_PRTOS_OFFSET + CONFIG_PRTOS_LOAD_ADDR);

    eprintf("-----------------------------------------------------------\n");
    {
        prtos_s32_t e, a;
        prtos_address_t st, end, v_start, v_end;
        for (e = 0; e < prtos_conf_table.num_of_partitions; e++) {
            eprintf("P%d (\"%s\":%d:%d) flags: [", e, &prtos_conf_string_tab[prtos_conf_partition_table[e].name_offset], prtos_conf_partition_table[e].id,
                    prtos_conf_partition_table[e].num_of_vcpus);
            if (prtos_conf_partition_table[e].flags & PRTOS_PART_SYSTEM) eprintf(" SYSTEM");
            if (prtos_conf_partition_table[e].flags & PRTOS_PART_FP) eprintf(" FP");
            eprintf(" ]:\n");

            for (a = 0; a < prtos_conf_partition_table[e].num_of_physical_memory_areas; a++) {
                st = prtos_conf_phys_mem_area_table[a + prtos_conf_partition_table[e].physical_memory_areas_offset].start_addr;
                end = st + prtos_conf_phys_mem_area_table[a + prtos_conf_partition_table[e].physical_memory_areas_offset].size - 1;
                v_start = prtos_conf_phys_mem_area_table[a + prtos_conf_partition_table[e].physical_memory_areas_offset].mapped_at;
                v_end = v_start + prtos_conf_phys_mem_area_table[a + prtos_conf_partition_table[e].physical_memory_areas_offset].size - 1;

                eprintf("    [0x%lx:0x%lx - 0x%lx:0x%lx]", st, v_start, end, v_end);
                eprintf(" flags: 0x%llx", prtos_conf_phys_mem_area_table[a + prtos_conf_partition_table[e].physical_memory_areas_offset].flags);
                eprintf("\n");

                eprintf("prtos_conf_boot_partition_table[%d].flags:0x%llx\n", e, prtos_conf_boot_partition_table[e].flags);
                eprintf("prtos_conf_boot_partition_table[%d].hdr_phys_addr:0x%llx\n", e, prtos_conf_boot_partition_table[e].hdr_phys_addr);
                eprintf("prtos_conf_boot_partition_table[%d].entry_point:0x%llx\n", e, prtos_conf_boot_partition_table[e].entry_point);
                eprintf("prtos_conf_boot_partition_table[%d].image_start:0x%llx\n", e, prtos_conf_boot_partition_table[e].image_start);
                eprintf("prtos_conf_boot_partition_table[%d].img_size:0x%llx\n", e, prtos_conf_boot_partition_table[e].img_size);
                eprintf("prtos_conf_boot_partition_table[%d].num_of_custom_files:0x%llx\n", e, prtos_conf_boot_partition_table[e].num_of_custom_files);
            }
        }
    }
    eprintf("-----------------------------------------------------------\n");

    eprintf("prtos_conf_rsw_info->entry_point: 0x%llx\n", prtos_conf_rsw_info->entry_point);

    eprintf("-----------------------------------------------------------\n");
    {
        prtos_s32_t e;
        prtos_address_t a, b;
        for (e = 0; e < prtos_conf_table.num_of_regions; e++) {
            a = prtos_conf_mem_reg_table[e].start_addr;
            b = a + prtos_conf_mem_reg_table[e].size - 1;
            eprintf("prtos_conf_mem_reg_table[%d].start_addr: 0x%llx, end_addr: 0x%llx\n", e, a, b);
            eprintf("prtos_conf_mem_reg_table[%d].flags: 0x%llx\n", e, prtos_conf_mem_reg_table[e].flags);
            eprintf("prtos_conf_mem_reg_table[%d].size: 0x%llx\n", e, prtos_conf_mem_reg_table[e].size);
        }
    }
    eprintf("-----------------------------------------------------------\n");
    {
        prtos_s32_t e;
        prtos_address_t a, b;
        for (e = 0; e < prtos_conf_table.num_of_physical_memory_areas; e++) {
            a = prtos_conf_phys_mem_area_table[e].start_addr;
            b = a + prtos_conf_phys_mem_area_table[e].size - 1;
            eprintf("prtos_conf_phys_mem_area_table[%d].start_addr: 0x%llx, end_addr: 0x%llx\n", e, a, b);
            eprintf("prtos_conf_phys_mem_area_table[%d].flags: 0x%llx\n", e, prtos_conf_phys_mem_area_table[e].flags);
            eprintf("prtos_conf_phys_mem_area_table[%d].mapped_at: 0x%llx\n", e, prtos_conf_phys_mem_area_table[e].mapped_at);
            eprintf("prtos_conf_phys_mem_area_table[%d].memory_region_offset: 0x%llx\n", e, prtos_conf_phys_mem_area_table[e].memory_region_offset);
            eprintf("prtos_conf_phys_mem_area_table[%d].name_offset: 0x%llx\n", e, prtos_conf_phys_mem_area_table[e].name_offset);
            eprintf("prtos_conf_phys_mem_area_table[%d].name: %s\n", e, &prtos_conf_string_tab[prtos_conf_phys_mem_area_table[e].name_offset]);
        }
    }

    eprintf("-----------------------------------------------------------\n");
    {
        prtos_s32_t e;
        for (e = 0; e < prtos_conf_table.num_of_comm_channels; e++) {
            eprintf("DOTO:Should print channel info...\n");
        }
    }
    eprintf("-----------------------------------------------------------\n");
    {
        prtos_s32_t e;
        for (e = 0; e < prtos_conf_table.num_of_comm_ports; e++) {
            eprintf("DOTO:Should print channel ports...\n");
        }
    }
    eprintf("-----------------------------------------------------------\n");
    {
        prtos_s32_t e;
        for (e = 0; e < prtos_conf_table.num_of_sched_cyclic_slots; e++) {
            eprintf("prtos_conf_sched_cyclic_slot_table[%d].id: %d\n", e, prtos_conf_sched_cyclic_slot_table[e].id);
            eprintf("prtos_conf_sched_cyclic_slot_table[%d].partition_id: %d\n", e, prtos_conf_sched_cyclic_slot_table[e].partition_id);
            eprintf("prtos_conf_sched_cyclic_slot_table[%d].vcpu_id: %d\n", e, prtos_conf_sched_cyclic_slot_table[e].vcpu_id);
            eprintf("prtos_conf_sched_cyclic_slot_table[%d].start_exec: %d\n", e, prtos_conf_sched_cyclic_slot_table[e].start_exec);
            eprintf("prtos_conf_sched_cyclic_slot_table[%d].end_exec: %d\n", e, prtos_conf_sched_cyclic_slot_table[e].end_exec);
        }
    }
    eprintf("-----------------------------------------------------------\n");

    {
        prtos_s32_t e;
        for (e = 0; e < prtos_conf_table.num_of_sched_cyclic_plans; e++) {
            eprintf("prtos_conf_sched_cyclic_plan_table[%d].name_offset: %d\n", e, prtos_conf_sched_cyclic_plan_table[e].name_offset);
            eprintf("prtos_conf_sched_cyclic_plan_table[%d].name: %s\n", e, &prtos_conf_string_tab[prtos_conf_sched_cyclic_plan_table[e].name_offset]);
            eprintf("prtos_conf_sched_cyclic_plan_table[%d].id: %d\n", e, prtos_conf_sched_cyclic_plan_table[e].id);
            eprintf("prtos_conf_sched_cyclic_plan_table[%d].major_frame: %d\n", e, prtos_conf_sched_cyclic_plan_table[e].major_frame);
            eprintf("prtos_conf_sched_cyclic_plan_table[%d].num_of_slots: %d\n", e, prtos_conf_sched_cyclic_plan_table[e].num_of_slots);
            eprintf("prtos_conf_sched_cyclic_plan_table[%d].slots_offset: %d\n", e, prtos_conf_sched_cyclic_plan_table[e].slots_offset);
        }
    }
    eprintf("-----------------------------------------------------------\n");

#endif

    init_rsv_mem();

#if defined(CONFIG_AARCH64)

    extern void setup_pagetables(unsigned long boot_phys_offset);
    unsigned long boot_phys_offset = -CONFIG_PRTOS_OFFSET + CONFIG_PRTOS_LOAD_ADDR;
    eprintf("boot_phys_offset: 0x%llx\n", boot_phys_offset);

#if 0
    prtos_kernel_run = 0;
    eprintf("Booting PRTOS Hypervisor on AArch64\n");
    void start_xen(unsigned long boot_phys_offset);
    start_xen(boot_phys_offset);
#endif

    extern void init_traps(void);
    extern void init_IRQ(void);
    void setup_mm(void);
    void vm_init(void);
    void smp_clear_cpu_maps(void);

    void init_percpu_areas_prtos();
    init_percpu_areas_prtos();

    init_traps();
    setup_pagetables(boot_phys_offset);
    smp_clear_cpu_maps();
    setup_mm();
    vm_init();

    // void  end_boot_allocator(void);
    // end_boot_allocator();

    init_IRQ();

    void init_global_clock_for_prtos(void);
    init_global_clock_for_prtos();

    early_setup_arch_common();
    setup_virt_mm();
    setup_phys_mm();
    setup_arch_common();
    create_local_info();
    setup_irqs();

    setup_kdev();
    setup_obj_dir();

    kprintf("PRTOS Hypervisor (%x.%x r%x)\n", (PRTOS_VERSION >> 16) & 0xFF, (PRTOS_VERSION >> 8) & 0xFF, PRTOS_VERSION & 0xFF);
    kprintf("Detected %lu.%luMHz processor.\n", (prtos_u32_t)(cpu_khz / 1000), (prtos_u32_t)(cpu_khz % 1000));
    barrier_lock(&smp_start_barrier);
    init_sched();
    setup_sys_clock();
    local_setup(cpu_id, idle);

    void gicv3_dt_preinit_prtos(void);
    gicv3_dt_preinit_prtos();  // gic_hw_ops is set to gicv3_ops

    void processor_id(void);
    processor_id();

    void smp_init_cpus_prtos(void);
    smp_init_cpus_prtos();

    unsigned int smp_get_max_cpus(void);
    int nr_cpu_ids = smp_get_max_cpus();
    eprintf("SMP: Allowing %u CPUs\n", nr_cpu_ids);

    int init_xen_time_prtos(void);
    init_xen_time_prtos();

    void gic_init(void);
    gic_init();  // Initialize GICv3

    void tasklet_subsys_init(void);
    tasklet_subsys_init();

    void init_maintenance_interrupt_prtos(void);
    init_maintenance_interrupt_prtos();  // Initialize maintenance interrupt

    void init_timer_interrupt_prtos(void);
    init_timer_interrupt_prtos();

    void timer_init(void);
    timer_init();

    void enable_timer_prtos(void);

    void init_idle_domain_prtos(void);
    init_idle_domain_prtos();  // Initialize idle domain for other CPUs, // TODO: should be called after smp_init_cpus_prtos()

    void rcu_init(void);
    rcu_init();

    local_irq_enable();

    void smp_prepare_cpus(void);
    smp_prepare_cpus();

    // void  do_presmp_initcalls(void);
    // do_presmp_initcalls();

    int cpu_up_prtos(unsigned int cpu);
    // int ret = cpu_up_prtos(1);
    // if ( ret != 0 )
    //     printk("Failed to bring up CPU %u (error %d)\n", 1, ret);

    void kick_cpus_prtos(void);
    kick_cpus_prtos();  // TO kick all CPUs

    enable_timer_prtos();  // test timer
    eprintf("Run to here.......\n");
    while (1) {
        prtos_u32_t i;
        for (i = 0; i < 1000000; i++) {
            asm volatile("nop");
        }
        // printk("PRTOS: Running...\n");
    };
#endif
    early_setup_arch_common();
    setup_virt_mm();
    setup_phys_mm();
    setup_arch_common();
    create_local_info();
    setup_irqs();

    setup_kdev();
    setup_obj_dir();

    kprintf("PRTOS Hypervisor (%x.%x r%x)\n", (PRTOS_VERSION >> 16) & 0xFF, (PRTOS_VERSION >> 8) & 0xFF, PRTOS_VERSION & 0xFF);
    kprintf("Detected %lu.%luMHz processor.\n", (prtos_u32_t)(cpu_khz / 1000), (prtos_u32_t)(cpu_khz % 1000));
    barrier_lock(&smp_start_barrier);
    init_sched();
    setup_sys_clock();
    local_setup(cpu_id, idle);
#ifdef CONFIG_SMP
    setup_smp();
    barrier_wait_mask(&smp_barrier_mask);
#endif
    setup_partitions();
#if CONFIG_DEBUG
    rsv_mem_debug();
#endif
    free_boot_mem();
}

#ifdef CONFIG_SMP

void __VBOOT init_secondary_cpu(prtos_s32_t cpu_id, kthread_t *idle) {
    ASSERT(GET_CPU_ID() != 0);

    local_setup(cpu_id, idle);
    barrier_wait(&smp_start_barrier);
    GET_LOCAL_PROCESSOR()->sched.flags |= LOCAL_SCHED_ENABLED;
    schedule();
    idle_task();
}

#endif
