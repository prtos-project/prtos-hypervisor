/* PRTOS SMP - consolidated */
/* === BEGIN INLINED: arch_arm_arm64_smpboot.c === */
#include <prtos_prtos_config.h>
#include <prtos_acpi.h>
#include <prtos_cpu.h>
#include <prtos_device_tree.h>
#include <prtos_lib.h>
#include <prtos_init.h>
#include <prtos_errno.h>
#include <prtos_mm.h>
#include <prtos_smp.h>
#include <prtos_vmap.h>
#include <asm_io.h>
#include <asm_psci.h>

/* Forward declaration */
extern int __init psci_init_prtos(void);

struct smp_enable_ops {
        int             (*prepare_cpu)(int cpu);
};

static paddr_t cpu_release_addr[NR_CPUS];
static struct smp_enable_ops smp_enable_ops[NR_CPUS];

static int __init smp_spin_table_cpu_up(int cpu)
{
    paddr_t __iomem *release;

    if (!cpu_release_addr[cpu])
    {
        printk("CPU%d: No release addr\n", cpu);
        return -ENODEV;
    }

    release = ioremap_nocache(cpu_release_addr[cpu], 8);
    if ( !release )
    {
        dprintk(PRTOSLOG_ERR, "CPU%d: Unable to map release address\n", cpu);
        return -EFAULT;
    }

    writeq(__pa(init_secondary), release);

    iounmap(release);

    sev();

    return 0;
}

static void __init smp_spin_table_init(int cpu, struct dt_device_node *dn)
{
    if ( !dt_property_read_u64(dn, "cpu-release-addr", &cpu_release_addr[cpu]) )
    {
        printk("CPU%d has no cpu-release-addr\n", cpu);
        return;
    }

    smp_enable_ops[cpu].prepare_cpu = smp_spin_table_cpu_up;
}

static int __init smp_psci_init(int cpu)
{
    if ( !psci_ver )
    {
        printk("CPU%d asks for PSCI, but DTB has no PSCI node\n", cpu);
        return -ENODEV;
    }

    smp_enable_ops[cpu].prepare_cpu = call_psci_cpu_on;
    return 0;
}

int __init arch_smp_init(void)
{
    /* Nothing */
    return 0;
}

static int __init dt_arch_cpu_init(int cpu, struct dt_device_node *dn)
{
    const char *enable_method;

    enable_method = dt_get_property(dn, "enable-method", NULL);
    if (!enable_method)
    {
        printk("CPU%d has no enable method\n", cpu);
        return -EINVAL;
    }

    if ( !strcmp(enable_method, "spin-table") )
        smp_spin_table_init(cpu, dn);
    else if ( !strcmp(enable_method, "psci") )
        return smp_psci_init(cpu);
    else
    {
        printk("CPU%d has unknown enable method \"%s\"\n", cpu, enable_method);
        return -EINVAL;
    }

    return 0;
}

int __init arch_cpu_init(int cpu, struct dt_device_node *dn)
{
    if ( acpi_disabled )
        return dt_arch_cpu_init(cpu, dn);
    else
        /* acpi only supports psci at present */
        return smp_psci_init(cpu);
}

int __init arch_cpu_init_prtos(int cpu, struct dt_device_node *dn)
{
    // if ( acpi_disabled )
    //     return dt_arch_cpu_init(cpu, dn);
    // else
    //     /* acpi only supports psci at present */
        return smp_psci_init(cpu);
}

int arch_cpu_up(int cpu)
{
    int rc;

    if ( !smp_enable_ops[cpu].prepare_cpu )
        return -ENODEV;

    update_identity_mapping(true);

    rc = smp_enable_ops[cpu].prepare_cpu(cpu);
    if ( rc )
        update_identity_mapping(false);

    return rc;
}

void arch_cpu_up_finish(void)
{
    update_identity_mapping(false);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: arch_arm_arm64_smpboot.c === */
/* === BEGIN INLINED: smpboot.c === */
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * prtos/arch/arm/smpboot.c
 *
 * Dummy smpboot support
 *
 * Copyright (c) 2011 Citrix Systems.
 */

#include <prtos_acpi.h>
#include <prtos_cpu.h>
#include <prtos_cpumask.h>
#include <prtos_delay.h>
#include <prtos_device_tree.h>
#include <prtos_domain_page.h>
#include <prtos_errno.h>
#include <prtos_init.h>
#include <prtos_mm.h>
#include <prtos_param.h>
#include <prtos_sched.h>
#include <prtos_smp.h>
#include <prtos_softirq.h>
#include <prtos_timer.h>
#include <prtos_warning.h>
#include <prtos_irq.h>
#include <prtos_console.h>
#include <asm_cpuerrata.h>
#include <asm_gic.h>
#include <asm_procinfo.h>
#include <asm_psci.h>
#include <asm_acpi.h>
#include <asm_tee_tee.h>

/* Override macros from asm/page.h to make them work with mfn_t */
#undef virt_to_mfn
#define virt_to_mfn(va) _mfn(__virt_to_mfn(va))

cpumask_t cpu_online_map;
cpumask_t cpu_present_map;
cpumask_t cpu_possible_map;

struct cpuinfo_arm cpu_data[NR_CPUS];

/* maxcpus: maximum number of CPUs to activate. */
static unsigned int __initdata max_cpus;
integer_param("maxcpus", max_cpus);

/* CPU logical map: map prtos cpuid to an MPIDR */
register_t __cpu_logical_map[NR_CPUS] = { [0 ... NR_CPUS-1] = MPIDR_INVALID };

/* Fake one node for now. See also prtos/numa.h */
nodemask_t __read_mostly node_online_map = { { [0] = 1UL } };

/* PRTOS stack for bringing up the first CPU. */
static unsigned char __initdata cpu0_boot_stack[STACK_SIZE]
       __attribute__((__aligned__(STACK_SIZE)));

/* Boot cpu data */
struct init_info init_data =
{
    .stack = cpu0_boot_stack,
};

/* Shared state for coordinating CPU bringup */
unsigned long __section(".data.idmap") smp_up_cpu = MPIDR_INVALID;
/* Shared state for coordinating CPU teardown */
static bool cpu_is_dead;

/* ID of the PCPU we're running on */
DEFINE_PER_CPU(unsigned int, cpu_id);
/*
 * Although multithread is part of the Arm spec, there are not many
 * processors supporting multithread and current PRTOS on Arm assumes there
 * is no multithread.
 */
/* representing HT siblings of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_sibling_mask);
/* representing HT and core siblings of each logical CPU */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_core_mask);

/*
 * By default non-boot CPUs not identical to the boot CPU will be
 * parked.
 */
static bool __read_mostly opt_hmp_unsafe = false;
boolean_param("hmp-unsafe", opt_hmp_unsafe);

static int setup_cpu_sibling_map(int cpu)
{
    if ( !zalloc_cpumask_var(&per_cpu(cpu_sibling_mask, cpu)) ||
         !zalloc_cpumask_var(&per_cpu(cpu_core_mask, cpu)) )
        return -ENOMEM;

    /*
     * Currently we assume there is no multithread and NUMA, so
     * a CPU is a sibling with itself, and the all possible CPUs
     * are supposed to belong to the same socket (NUMA node).
     */
    cpumask_set_cpu(cpu, per_cpu(cpu_sibling_mask, cpu));
    cpumask_copy(per_cpu(cpu_core_mask, cpu), &cpu_possible_map);

    return 0;
}

static void remove_cpu_sibling_map(int cpu)
{
    free_cpumask_var(per_cpu(cpu_sibling_mask, cpu));
    free_cpumask_var(per_cpu(cpu_core_mask, cpu));
}

void __init
smp_clear_cpu_maps (void)
{
    cpumask_clear(&cpu_possible_map);
    cpumask_clear(&cpu_online_map);
    cpumask_set_cpu(0, &cpu_online_map);
    cpumask_set_cpu(0, &cpu_possible_map);
    cpu_logical_map(0) = READ_SYSREG(MPIDR_EL1) & MPIDR_HWID_MASK;
}

/* Parse the device tree and build the logical map array containing
 * MPIDR values related to logical cpus
 * Code base on Linux arch/arm/kernel/devtree.c
 */
static void __init dt_smp_init_cpus(void)
{
    register_t mpidr;
    struct dt_device_node *cpus = dt_find_node_by_path("/cpus");
    struct dt_device_node *cpu;
    unsigned int i, j;
    unsigned int cpuidx = 1;
    static register_t tmp_map[NR_CPUS] __initdata =
    {
        [0 ... NR_CPUS - 1] = MPIDR_INVALID
    };
    bool bootcpu_valid = false;
    int rc;

    mpidr = system_cpuinfo.mpidr.bits & MPIDR_HWID_MASK;

    if ( !cpus )
    {
        printk(PRTOSLOG_WARNING "WARNING: Can't find /cpus in the device tree.\n"
               "Using only 1 CPU\n");
        return;
    }

    dt_for_each_child_node( cpus, cpu )
    {
        const __be32 *prop;
        u64 addr;
        u32 reg_len;
        register_t hwid;

        if ( !dt_device_type_is_equal(cpu, "cpu") )
            continue;

        if ( dt_n_size_cells(cpu) != 0 )
            printk(PRTOSLOG_WARNING "cpu node `%s`: #size-cells %d\n",
                   dt_node_full_name(cpu), dt_n_size_cells(cpu));

        prop = dt_get_property(cpu, "reg", &reg_len);
        if ( !prop )
        {
            printk(PRTOSLOG_WARNING "cpu node `%s`: has no reg property\n",
                   dt_node_full_name(cpu));
            continue;
        }

        if ( reg_len < dt_cells_to_size(dt_n_addr_cells(cpu)) )
        {
            printk(PRTOSLOG_WARNING "cpu node `%s`: reg property too short\n",
                   dt_node_full_name(cpu));
            continue;
        }

        addr = dt_read_paddr(prop, dt_n_addr_cells(cpu));

        hwid = addr;
        if ( hwid != addr )
        {
            printk(PRTOSLOG_WARNING "cpu node `%s`: hwid overflow %"PRIx64"\n",
                   dt_node_full_name(cpu), addr);
            continue;
        }

        /*
         * 8 MSBs must be set to 0 in the DT since the reg property
         * defines the MPIDR[23:0]
         */
        if ( hwid & ~MPIDR_HWID_MASK )
        {
            printk(PRTOSLOG_WARNING "cpu node `%s`: invalid hwid value (0x%"PRIregister")\n",
                   dt_node_full_name(cpu), hwid);
            continue;
        }

        /*
         * Duplicate MPIDRs are a recipe for disaster. Scan all initialized
         * entries and check for duplicates. If any found just skip the node.
         * temp values values are initialized to MPIDR_INVALID to avoid
         * matching valid MPIDR[23:0] values.
         */
        for ( j = 0; j < cpuidx; j++ )
        {
            if ( tmp_map[j] == hwid )
            {
                printk(PRTOSLOG_WARNING
                       "cpu node `%s`: duplicate /cpu reg properties %"PRIregister" in the DT\n",
                       dt_node_full_name(cpu), hwid);
                break;
            }
        }
        if ( j != cpuidx )
            continue;

        /*
         * Build a stashed array of MPIDR values. Numbering scheme requires
         * that if detected the boot CPU must be assigned logical id 0. Other
         * CPUs get sequential indexes starting from 1. If a CPU node
         * with a reg property matching the boot CPU MPIDR is detected,
         * this is recorded and so that the logical map build from DT is
         * validated and can be used to set the map.
         */
        if ( hwid == mpidr )
        {
            i = 0;
            bootcpu_valid = true;
        }
        else
            i = cpuidx++;

        if ( cpuidx > NR_CPUS )
        {
            printk(PRTOSLOG_WARNING
                   "DT /cpu %u node greater than max cores %u, capping them\n",
                   cpuidx, NR_CPUS);
            cpuidx = NR_CPUS;
            break;
        }

        if ( (rc = arch_cpu_init(i, cpu)) < 0 )
        {
            printk("cpu%d init failed (hwid %"PRIregister"): %d\n", i, hwid, rc);
            tmp_map[i] = MPIDR_INVALID;
        }
        else
            tmp_map[i] = hwid;
    }

    if ( !bootcpu_valid )
    {
        printk(PRTOSLOG_WARNING "DT missing boot CPU MPIDR[23:0]\n"
               "Using only 1 CPU\n");
        return;
    }

    for ( i = 0; i < cpuidx; i++ )
    {
        if ( tmp_map[i] == MPIDR_INVALID )
            continue;
        cpumask_set_cpu(i, &cpu_possible_map);
        cpu_logical_map(i) = tmp_map[i];
    }
}

static void __init dt_smp_init_cpus_prtos(void)
{
    register_t mpidr;
    // struct dt_device_node *cpus = dt_find_node_by_path("/cpus");
    // struct dt_device_node *cpu;
    unsigned int i __attribute__((unused)), j __attribute__((unused));
    unsigned int cpuidx = 1;
    static register_t tmp_map[NR_CPUS] __initdata =
    {
        [0 ... NR_CPUS - 1] = MPIDR_INVALID
    };
    bool bootcpu_valid __attribute__((unused)) = false;
    int rc;

    mpidr = system_cpuinfo.mpidr.bits & MPIDR_HWID_MASK;

    // if ( !cpus )
    // {
    //     printk(PRTOSLOG_WARNING "WARNING: Can't find /cpus in the device tree.\n"
    //            "Using only 1 CPU\n");
    //     return;
    // }

    // dt_for_each_child_node( cpus, cpu )
    // {
    //     const __be32 *prop;
    //     u64 addr;
    //     u32 reg_len;
    //     register_t hwid;

    //     if ( !dt_device_type_is_equal(cpu, "cpu") )
    //         continue;

    //     if ( dt_n_size_cells(cpu) != 0 )
    //         printk(PRTOSLOG_WARNING "cpu node `%s`: #size-cells %d\n",
    //                dt_node_full_name(cpu), dt_n_size_cells(cpu));

    //     prop = dt_get_property(cpu, "reg", &reg_len);
    //     if ( !prop )
    //     {
    //         printk(PRTOSLOG_WARNING "cpu node `%s`: has no reg property\n",
    //                dt_node_full_name(cpu));
    //         continue;
    //     }

    //     if ( reg_len < dt_cells_to_size(dt_n_addr_cells(cpu)) )
    //     {
    //         printk(PRTOSLOG_WARNING "cpu node `%s`: reg property too short\n",
    //                dt_node_full_name(cpu));
    //         continue;
    //     }

    //     addr = dt_read_paddr(prop, dt_n_addr_cells(cpu));

    //     hwid = addr;
    //     if ( hwid != addr )
    //     {
    //         printk(PRTOSLOG_WARNING "cpu node `%s`: hwid overflow %"PRIx64"\n",
    //                dt_node_full_name(cpu), addr);
    //         continue;
    //     }

    //     /*
    //      * 8 MSBs must be set to 0 in the DT since the reg property
    //      * defines the MPIDR[23:0]
    //      */
    //     if ( hwid & ~MPIDR_HWID_MASK )
    //     {
    //         printk(PRTOSLOG_WARNING "cpu node `%s`: invalid hwid value (0x%"PRIregister")\n",
    //                dt_node_full_name(cpu), hwid);
    //         continue;
    //     }

    //     /*
    //      * Duplicate MPIDRs are a recipe for disaster. Scan all initialized
    //      * entries and check for duplicates. If any found just skip the node.
    //      * temp values values are initialized to MPIDR_INVALID to avoid
    //      * matching valid MPIDR[23:0] values.
    //      */
    //     for ( j = 0; j < cpuidx; j++ )
    //     {
    //         if ( tmp_map[j] == hwid )
    //         {
    //             printk(PRTOSLOG_WARNING
    //                    "cpu node `%s`: duplicate /cpu reg properties %"PRIregister" in the DT\n",
    //                    dt_node_full_name(cpu), hwid);
    //             break;
    //         }
    //     }
    //     if ( j != cpuidx )
    //         continue;

    //     /*
    //      * Build a stashed array of MPIDR values. Numbering scheme requires
    //      * that if detected the boot CPU must be assigned logical id 0. Other
    //      * CPUs get sequential indexes starting from 1. If a CPU node
    //      * with a reg property matching the boot CPU MPIDR is detected,
    //      * this is recorded and so that the logical map build from DT is
    //      * validated and can be used to set the map.
    //      */
    //     if ( hwid == mpidr )
    //     {
    //         i = 0;
    //         bootcpu_valid = true;
    //     }
    //     else
    //         i = cpuidx++;

    //     if ( cpuidx > NR_CPUS )
    //     {
    //         printk(PRTOSLOG_WARNING
    //                "DT /cpu %u node greater than max cores %u, capping them\n",
    //                cpuidx, NR_CPUS);
    //         cpuidx = NR_CPUS;
    //         break;
    //     }

    //     if ( (rc = arch_cpu_init(i, cpu)) < 0 )
    //     {
    //         printk("cpu%d init failed (hwid %"PRIregister"): %d\n", i, hwid, rc);
    //         tmp_map[i] = MPIDR_INVALID;
    //     }
    //     else
    //         tmp_map[i] = hwid;
    // }

    // if ( !bootcpu_valid )
    // {
    //     printk(PRTOSLOG_WARNING "DT missing boot CPU MPIDR[23:0]\n"
    //            "Using only 1 CPU\n");
    //     return;
    // }

    for( i = 0; i < 4; i++ ) {
        if ( (rc = arch_cpu_init_prtos(i, NULL)) < 0 )
        {
            printk("cpu%d init failed (hwid %u): %d\n", i, i, rc);
            tmp_map[i] = MPIDR_INVALID;
        }
        else
            tmp_map[i] = i;
    }

    // tmp_map[0] = 0;
    // tmp_map[1] = 1;
    // tmp_map[2] = 2;
    // tmp_map[3] = 3;
    cpuidx = 4;
    for ( i = 0; i < cpuidx; i++ )
    {
        if ( tmp_map[i] == MPIDR_INVALID )
            continue;
        cpumask_set_cpu(i, &cpu_possible_map);
        cpu_logical_map(i) = tmp_map[i];
    }
}

void __init smp_init_cpus(void)
{
    int rc;

    /* initialize PSCI and set a global variable */
    psci_init();

    if ( (rc = arch_smp_init()) < 0 )
    {
        printk(PRTOSLOG_WARNING "SMP init failed (%d)\n"
               "Using only 1 CPU\n", rc);
        return;
    }

    if ( acpi_disabled )
        dt_smp_init_cpus();
    else
        acpi_smp_init_cpus();

    if ( opt_hmp_unsafe )
        warning_add("WARNING: HMP COMPUTING HAS BEEN ENABLED.\n"
                    "It has implications on the security and stability of the system,\n"
                    "unless the cpu affinity of all domains is specified.\n");

    if ( system_cpuinfo.mpidr.mt == 1 )
        warning_add("WARNING: MULTITHREADING HAS BEEN DETECTED ON THE PROCESSOR.\n"
                    "It might impact the security of the system.\n");
}

void __init smp_init_cpus_prtos(void)
{
    int rc;

    /* initialize PSCI and set a global variable */
    psci_init_prtos();

    if ( (rc = arch_smp_init()) < 0 )
    {
        printk(PRTOSLOG_WARNING "SMP init failed (%d)\n"
               "Using only 1 CPU\n", rc);
        return;
    }

    // if ( acpi_disabled )
        dt_smp_init_cpus_prtos();
    // else
    //     acpi_smp_init_cpus();

    if ( opt_hmp_unsafe )
        warning_add("WARNING: HMP COMPUTING HAS BEEN ENABLED.\n"
                    "It has implications on the security and stability of the system,\n"
                    "unless the cpu affinity of all domains is specified.\n");

    if ( system_cpuinfo.mpidr.mt == 1 )
        warning_add("WARNING: MULTITHREADING HAS BEEN DETECTED ON THE PROCESSOR.\n"
                    "It might impact the security of the system.\n");
}


unsigned int __init smp_get_max_cpus(void)
{
    unsigned int i, cpus = 0;

    if ( ( !max_cpus ) || ( max_cpus > nr_cpu_ids ) )
        max_cpus = nr_cpu_ids;

    for ( i = 0; i < max_cpus; i++ )
        if ( cpu_possible(i) )
            cpus++;

    return cpus;
}

void __init
smp_prepare_cpus(void)
{
    int rc;

    cpumask_copy(&cpu_present_map, &cpu_possible_map);

    rc = setup_cpu_sibling_map(0);
    if ( rc )
        panic("Unable to allocate CPU sibling/core maps\n");

}

/* Boot the current CPU */
void asmlinkage start_secondary(void)
{
    unsigned int cpuid = init_data.cpuid;

    memset(get_cpu_info(), 0, sizeof (struct cpu_info));

    set_processor_id(cpuid);

    identify_cpu(&current_cpu_data);
    processor_setup();

    init_traps();

    /*
     * Currently PRTOS assumes the platform has only one kind of CPUs.
     * This assumption does not hold on big.LITTLE platform and may
     * result to instability and insecure platform (unless cpu affinity
     * is manually specified for all domains). Better to park them for
     * now.
     */
    if ( current_cpu_data.midr.bits != system_cpuinfo.midr.bits )
    {
        if ( !opt_hmp_unsafe )
        {
            printk(PRTOSLOG_ERR
                   "CPU%u MIDR (0x%"PRIregister") does not match boot CPU MIDR (0x%"PRIregister"),\n"
                   PRTOSLOG_ERR "disable cpu (see big.LITTLE.txt under docs/).\n",
                   smp_processor_id(), current_cpu_data.midr.bits,
                   system_cpuinfo.midr.bits);
            stop_cpu();
        }
        else
        {
            printk(PRTOSLOG_ERR
                   "CPU%u MIDR (0x%"PRIregister") does not match boot CPU MIDR (0x%"PRIregister"),\n"
                   PRTOSLOG_ERR "hmp-unsafe turned on so tainting PRTOS and keep core on!!\n",
                   smp_processor_id(), current_cpu_data.midr.bits,
                   system_cpuinfo.midr.bits);
            add_taint(TAINT_CPU_OUT_OF_SPEC);
         }
    }

    if ( dcache_line_bytes != read_dcache_line_bytes() )
    {
        printk(PRTOSLOG_ERR "CPU%u dcache line size (%zu) does not match the boot CPU (%zu)\n",
               smp_processor_id(), read_dcache_line_bytes(),
               dcache_line_bytes);
        stop_cpu();
    }

    /*
     * system features must be updated only if we do not stop the core or
     * we might disable features due to a non used core (for example when
     * booting on big cores on a big.LITTLE system with hmp_unsafe)
     */
    update_system_features(&current_cpu_data);

    gic_init_secondary_cpu();

    set_current(idle_vcpu[cpuid]);

    /* Initialize vGIC lists for this CPU's idle vcpu so that
     * vgic_sync_to_lrs() in leave_hypervisor_to_guest() doesn't
     * dereference NULL list pointers on the first EL2→EL1 return. */
    INIT_LIST_HEAD(&idle_vcpu[cpuid]->arch.vgic.inflight_irqs);
    INIT_LIST_HEAD(&idle_vcpu[cpuid]->arch.vgic.lr_pending);
    spin_lock_init(&idle_vcpu[cpuid]->arch.vgic.lock);

    /* Run local notifiers */
    // notify_cpu_starting(cpuid);
    /*
     * Ensure that previous writes are visible before marking the cpu as
     * online.
     */
    smp_wmb();

    /* Now report this CPU is up */
    cpumask_set_cpu(cpuid, &cpu_online_map);

    local_irq_enable();

    /*
     * Calling request_irq() after local_irq_enable() on secondary cores
     * will make sure the assertion condition in alloc_prtosheap_pages(),
     * i.e. !in_irq && local_irq_enabled() is satisfied.
     */
    init_maintenance_interrupt();
    init_timer_interrupt();
    init_tee_secondary();

    local_abort_enable();

    check_local_cpu_errata();
    check_local_cpu_features();

    printk(PRTOSLOG_DEBUG "CPU %u booted.\n", smp_processor_id());

    startup_cpu_idle_loop();
}

/* Shut down the current CPU */
void __cpu_disable(void)
{
    unsigned int cpu = smp_processor_id();

    local_irq_disable();
    gic_disable_cpu();
    /* Allow any queued timer interrupts to get serviced */
    local_irq_enable();
    mdelay(1);
    local_irq_disable();

    /* It's now safe to remove this processor from the online map */
    cpumask_clear_cpu(cpu, &cpu_online_map);

    smp_mb();

    /* Return to caller; eventually the IPI mechanism will unwind and the 
     * scheduler will drop to the idle loop, which will call stop_cpu(). */
}

void stop_cpu(void)
{
    local_irq_disable();
    cpu_is_dead = true;
    /* Make sure the write happens before we sleep forever */
    dsb(sy);
    isb();
    call_psci_cpu_off();

    while ( 1 )
        wfi();
}

static void set_smp_up_cpu(unsigned long mpidr)
{
    /*
     * smp_up_cpu is part of the identity mapping which is read-only. So
     * We need to re-map the region so it can be updated.
     */
    void *ptr = map_domain_page(virt_to_mfn(&smp_up_cpu));

    ptr += PAGE_OFFSET(&smp_up_cpu);

    *(unsigned long *)ptr = mpidr;

    /*
     * smp_up_cpu will be accessed with the MMU off, so ensure the update
     * is visible by cleaning the cache.
     */
    clean_dcache_va_range(ptr, sizeof(unsigned long));

    unmap_domain_page(ptr);

}


/* Bring up a remote CPU */
int __cpu_up(unsigned int cpu)
{
    int rc;
    s_time_t deadline;

    printk("Bringing up CPU%d\n", cpu);

    rc = prepare_secondary_mm(cpu);
    if ( rc < 0 )
        return rc;

    console_start_sync(); /* Secondary may use early_printk */

    /* Tell the remote CPU which stack to boot on. */
    init_data.stack = idle_vcpu[cpu]->arch.stack;

    /* Tell the remote CPU what its logical CPU ID is. */
    init_data.cpuid = cpu;

    /* Open the gate for this CPU */
    set_smp_up_cpu(cpu_logical_map(cpu));

    rc = arch_cpu_up(cpu);

    console_end_sync();

    if ( rc < 0 )
    {
        printk("Failed to bring up CPU%d\n", cpu);
        return rc;
    }

    deadline = NOW() + MILLISECS(1000);

    while ( !cpu_online(cpu) && NOW() < deadline )
    {
        cpu_relax();
        process_pending_softirqs();
    }
    /*
     * Ensure that other cpus' initializations are visible before
     * proceeding. Corresponds to smp_wmb() in start_secondary.
     */
    smp_rmb();

    /*
     * Nuke start of day info before checking one last time if the CPU
     * actually came online. If it is not online it may still be
     * trying to come up and may show up later unexpectedly.
     *
     * This doesn't completely avoid the possibility of the supposedly
     * failed CPU trying to progress with another CPUs stack settings
     * etc, but better than nothing, hopefully.
     */
    init_data.stack = NULL;
    init_data.cpuid = ~0;

    set_smp_up_cpu(MPIDR_INVALID);

    arch_cpu_up_finish();

    if ( !cpu_online(cpu) )
    {
        printk("CPU%d never came online\n", cpu);
        return -EIO;
    }

    return 0;
}

/* Wait for a remote CPU to die */
void __cpu_die(unsigned int cpu)
{
    unsigned int i = 0;

    while ( !cpu_is_dead )
    {
        mdelay(100);
        cpu_relax();
        process_pending_softirqs();
        if ( (++i % 10) == 0 )
            printk(KERN_ERR "CPU %u still not dead...\n", cpu);
        smp_mb();
    }
    cpu_is_dead = false;
    smp_mb();
}

static int cpu_smpboot_callback(struct notifier_block *nfb,
                                unsigned long action,
                                void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;
    unsigned int rc = 0;

    switch ( action )
    {
    case CPU_UP_PREPARE:
        rc = setup_cpu_sibling_map(cpu);
        if ( rc )
            printk(PRTOSLOG_ERR
                   "Unable to allocate CPU sibling/core map  for CPU%u\n",
                   cpu);

        break;

    case CPU_DEAD:
        remove_cpu_sibling_map(cpu);
        break;
    default:
        break;
    }

    return notifier_from_errno(rc);
}

void setup_cpu_sibling_map_prtos(int cpu) {
    int rc = setup_cpu_sibling_map(cpu);
    if (rc) printk(PRTOSLOG_ERR "Unable to allocate CPU sibling/core map  for CPU%u\n", cpu);
}

static struct notifier_block cpu_smpboot_nfb = {
    .notifier_call = cpu_smpboot_callback,
};

static int __init cpu_smpboot_notifier_init(void)
{
    register_cpu_notifier(&cpu_smpboot_nfb);

    return 0;
}
presmp_initcall(cpu_smpboot_notifier_init);

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: smpboot.c === */
/* === BEGIN INLINED: cpu.c === */
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>

#include <prtos_cpumask.h>
#include <prtos_cpu.h>
#include <prtos_event.h>
#include <prtos_init.h>
#include <prtos_sched.h>
#include <prtos_stop_machine.h>
#include <prtos_rcupdate.h>

unsigned int __read_mostly nr_cpu_ids = NR_CPUS;
#ifndef nr_cpumask_bits
unsigned int __read_mostly nr_cpumask_bits
    = BITS_TO_LONGS(NR_CPUS) * BITS_PER_LONG;
#endif

const cpumask_t cpumask_all = {
    .bits[0 ... (BITS_TO_LONGS(NR_CPUS) - 1)] = ~0UL
};

/*
 * cpu_bit_bitmap[] is a special, "compressed" data structure that
 * represents all NR_CPUS bits binary values of 1<<nr.
 *
 * It is used by cpumask_of() to get a constant address to a CPU
 * mask value that has a single bit set only.
 */

/* cpu_bit_bitmap[0] is empty - so we can back into it */
#define MASK_DECLARE_1(x) [(x) + 1][0] = 1UL << (x)
#define MASK_DECLARE_2(x) MASK_DECLARE_1(x), MASK_DECLARE_1((x) + 1)
#define MASK_DECLARE_4(x) MASK_DECLARE_2(x), MASK_DECLARE_2((x) + 2)
#define MASK_DECLARE_8(x) MASK_DECLARE_4(x), MASK_DECLARE_4((x) + 4)

const unsigned long cpu_bit_bitmap[BITS_PER_LONG+1][BITS_TO_LONGS(NR_CPUS)] = {

    MASK_DECLARE_8(0),  MASK_DECLARE_8(8),
    MASK_DECLARE_8(16), MASK_DECLARE_8(24),
#if BITS_PER_LONG > 32
    MASK_DECLARE_8(32), MASK_DECLARE_8(40),
    MASK_DECLARE_8(48), MASK_DECLARE_8(56),
#endif
};

#undef MASK_DECLARE_8
#undef MASK_DECLARE_4
#undef MASK_DECLARE_2
#undef MASK_DECLARE_1

static DEFINE_RWLOCK(cpu_add_remove_lock);

bool get_cpu_maps(void)
{
    return read_trylock(&cpu_add_remove_lock);
}

void put_cpu_maps(void)
{
    read_unlock(&cpu_add_remove_lock);
}

void cpu_hotplug_begin(void)
{
    rcu_barrier();
    write_lock(&cpu_add_remove_lock);
}

void cpu_hotplug_done(void)
{
    write_unlock(&cpu_add_remove_lock);
}


static NOTIFIER_HEAD(cpu_chain);

void __init register_cpu_notifier(struct notifier_block *nb)
{
    write_lock(&cpu_add_remove_lock);
    notifier_chain_register(&cpu_chain, nb);
    write_unlock(&cpu_add_remove_lock);
}

static int cpu_notifier_call_chain(unsigned int cpu, unsigned long action,
                                   struct notifier_block **nb, bool nofail)
{
    void *hcpu = (void *)(long)cpu;
    int notifier_rc = notifier_call_chain(&cpu_chain, action, hcpu, nb);
    int ret =  notifier_to_errno(notifier_rc);

    BUG_ON(ret && nofail);

    return ret;
}

static void cf_check _take_cpu_down(void *unused)
{
    cpu_notifier_call_chain(smp_processor_id(), CPU_DYING, NULL, true);
    __cpu_disable();
}

static int cf_check take_cpu_down(void *arg)
{
    _take_cpu_down(arg);
    return 0;
}

int cpu_down(unsigned int cpu)
{
    int err;
    struct notifier_block *nb = NULL;

    cpu_hotplug_begin();

    err = -EINVAL;
    if ( (cpu >= nr_cpu_ids) || (cpu == 0) )
        goto out;

    err = -EEXIST;
    if ( !cpu_online(cpu) )
        goto out;

    err = cpu_notifier_call_chain(cpu, CPU_DOWN_PREPARE, &nb, false);
    if ( err )
        goto fail;

    if ( system_state < SYS_STATE_active || system_state == SYS_STATE_resume )
        on_selected_cpus(cpumask_of(cpu), _take_cpu_down, NULL, true);
    else if ( (err = stop_machine_run(take_cpu_down, NULL, cpu)) < 0 )
        goto fail;

    __cpu_die(cpu);
    err = cpu_online(cpu);
    BUG_ON(err);

    cpu_notifier_call_chain(cpu, CPU_DEAD, NULL, true);

    send_global_virq(VIRQ_PCPU_STATE);
    cpu_hotplug_done();
    return 0;

 fail:
    cpu_notifier_call_chain(cpu, CPU_DOWN_FAILED, &nb, true);
 out:
    cpu_hotplug_done();
    return err;
}

int cpu_up(unsigned int cpu)
{
    int err;
    struct notifier_block *nb = NULL;

    // cpu_hotplug_begin();

    err = -EINVAL;
    if ( (cpu >= nr_cpu_ids) || !cpu_present(cpu) )
        goto out;

    err = -EEXIST;
    if ( cpu_online(cpu) )
        goto out;

    err = cpu_notifier_call_chain(cpu, CPU_UP_PREPARE, &nb, false);
    if ( err )
        goto fail;

    err = __cpu_up(cpu);
    if ( err < 0 )
        goto fail;

    cpu_notifier_call_chain(cpu, CPU_ONLINE, NULL, true);

    send_global_virq(VIRQ_PCPU_STATE);

    // cpu_hotplug_done();
    return 0;

 fail:
    cpu_notifier_call_chain(cpu, CPU_UP_CANCELED, &nb, true);
 out:
    // cpu_hotplug_done();
    return err;
}


int init_percpu_area_prtos(unsigned int cpu);
int init_tasklet_softirq_tasklet_prtos(int cpu);
void init_timers_prtos(unsigned int cpu);
int init_local_irq_data_prtos(unsigned int cpu);
int cpu_schedule_up_prtos(unsigned int cpu);
void rcu_init_percpu_data_prtos(int cpu);
void calibrate_safe_atomic_prtos(void);
void setup_cpu_sibling_map_prtos(int cpu);
void cpu_lockdebug_init_prtos(int cpu);
void cpu_prepare_prtos(int cpu);

void cpu_prepare_init_prtos(int cpu) {
    init_percpu_area_prtos(cpu);
    init_tasklet_softirq_tasklet_prtos(cpu);
    init_timers_prtos(cpu);
    init_local_irq_data_prtos(cpu);
    cpu_schedule_up_prtos(cpu);
    rcu_init_percpu_data_prtos(cpu);
    calibrate_safe_atomic_prtos();
    setup_cpu_sibling_map_prtos(cpu);
    cpu_lockdebug_init_prtos(cpu);
}

int cpu_up_prtos(unsigned int cpu)
{
    int err;
    struct notifier_block *nb __attribute__((unused)) = NULL;

    // cpu_hotplug_begin();

    err = -EINVAL;
    if ( (cpu >= nr_cpu_ids) || !cpu_present(cpu) )
        goto out;

    err = -EEXIST;
    if ( cpu_online(cpu) )
        goto out;

    // err = cpu_notifier_call_chain(cpu, CPU_UP_PREPARE, &nb, false);
    // if ( err )
    //     goto fail;
    cpu_prepare_init_prtos(cpu);

    err = __cpu_up(cpu);
    if ( err < 0 )
        goto fail;

    // cpu_notifier_call_chain(cpu, CPU_ONLINE, NULL, true);

    send_global_virq(VIRQ_PCPU_STATE);

    // cpu_hotplug_done();
    return 0;

 fail:
    // cpu_notifier_call_chain(cpu, CPU_UP_CANCELED, &nb, true);
 out:
    // cpu_hotplug_done();
    return err;
}


/* === END INLINED: cpu.c === */
/* === BEGIN INLINED: common_smp.c === */
#include <prtos_prtos_config.h>
/*
 * prtos/common/smp.c
 *
 * Generic SMP function
 *
 * Copyright (c) 2013 Citrix Systems.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm_generic_hardirq.h>
#include <asm_processor.h>
#include <prtos_spinlock.h>
#include <prtos_smp.h>

/*
 * Structure and data for smp_call_function()/on_selected_cpus().
 */
static DEFINE_SPINLOCK(call_lock);
static struct call_data_struct {
    void (*func) (void *info);
    void *info;
    int wait;
    cpumask_t selected;
} call_data;

void smp_call_function(
    void (*func) (void *info),
    void *info,
    int wait)
{
    cpumask_t allbutself;

    cpumask_andnot(&allbutself, &cpu_online_map,
                   cpumask_of(smp_processor_id()));
    on_selected_cpus(&allbutself, func, info, wait);
}

void on_selected_cpus(
    const cpumask_t *selected,
    void (*func) (void *info),
    void *info,
    int wait)
{
    ASSERT(local_irq_is_enabled());
    ASSERT(cpumask_subset(selected, &cpu_online_map));

    spin_lock(&call_lock);

    cpumask_copy(&call_data.selected, selected);

    if ( cpumask_empty(&call_data.selected) )
        goto out;

    call_data.func = func;
    call_data.info = info;
    call_data.wait = wait;

    smp_send_call_function_mask(&call_data.selected);

    while ( !cpumask_empty(&call_data.selected) )
        cpu_relax();

out:
    spin_unlock(&call_lock);
}

void smp_call_function_interrupt(void)
{
    void (*func)(void *info) = call_data.func;
    void *info = call_data.info;
    unsigned int cpu = smp_processor_id();

    if ( !cpumask_test_cpu(cpu, &call_data.selected) )
        return;

    irq_enter();

    if ( unlikely(!func) )
    {
        cpumask_clear_cpu(cpu, &call_data.selected);
    }
    else if ( call_data.wait )
    {
        (*func)(info);
        smp_mb();
        cpumask_clear_cpu(cpu, &call_data.selected);
    }
    else
    {
        smp_mb();
        cpumask_clear_cpu(cpu, &call_data.selected);
        (*func)(info);
    }

    irq_exit();
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: common_smp.c === */
/* === BEGIN INLINED: percpu.c === */
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>
/* SPDX-License-Identifier: GPL-2.0 */
#include <prtos_prtos_config.h>

#include <prtos_percpu.h>
#include <prtos_cpu.h>
#include <prtos_init.h>
#include <prtos_mm.h>
#include <prtos_rcupdate.h>

unsigned long __per_cpu_offset[NR_CPUS];
#define INVALID_PERCPU_AREA (-(long)__per_cpu_start)
#define PERCPU_ORDER (get_order_from_bytes(__per_cpu_data_end-__per_cpu_start))

void __init percpu_init_areas(void)
{
    unsigned int cpu;
    for ( cpu = 1; cpu < NR_CPUS; cpu++ )
        __per_cpu_offset[cpu] = INVALID_PERCPU_AREA;
}
static char per_arrary_prtos[0x2000 * 4] __attribute__((aligned(4096))) = {0};
static int per_index = 0;
static int init_percpu_area(unsigned int cpu)
{
    char *p;
    if ( __per_cpu_offset[cpu] != INVALID_PERCPU_AREA )
        return -EBUSY;
    printk("init_percpu_area PERCPU_ORDER: 0x%x\n", PERCPU_ORDER);
    int Size = __per_cpu_data_end - __per_cpu_start;
    printk("init_percpu_area Size: 0x%x\n", Size);
    p = &per_arrary_prtos[0x2000 * per_index++];
    if (per_index >= 4) {
        printk("per_index overflow: %d\n", per_index);
        return -ENOMEM;
    }
    
    // if ( (p = alloc_prtosheap_pages(PERCPU_ORDER, 0)) == NULL )
    //     return -ENOMEM;
    memset(p, 0, __per_cpu_data_end - __per_cpu_start);
    __per_cpu_offset[cpu] = p - __per_cpu_start;
    return 0;
}

int init_percpu_area_prtos(unsigned int cpu) {
    return init_percpu_area(cpu);
}

struct free_info {
    unsigned int cpu;
    struct rcu_head rcu;
};
static DEFINE_PER_CPU(struct free_info, free_info);

static void _free_percpu_area(struct rcu_head *head)
{
    struct free_info *info = container_of(head, struct free_info, rcu);
    unsigned int cpu = info->cpu;
    char *p = __per_cpu_start + __per_cpu_offset[cpu];
    free_prtosheap_pages(p, PERCPU_ORDER);
    __per_cpu_offset[cpu] = INVALID_PERCPU_AREA;
}

static void free_percpu_area(unsigned int cpu)
{
    struct free_info *info = &per_cpu(free_info, cpu);
    info->cpu = cpu;
    call_rcu(&info->rcu, _free_percpu_area);
}

static int cpu_percpu_callback(
    struct notifier_block *nfb, unsigned long action, void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;
    int rc = 0;

    switch ( action )
    {
    case CPU_UP_PREPARE:
        rc = init_percpu_area(cpu);
        break;
    case CPU_UP_CANCELED:
    case CPU_DEAD:
        free_percpu_area(cpu);
        break;
    default:
        break;
    }

    return notifier_from_errno(rc);
}

static struct notifier_block cpu_percpu_nfb = {
    .notifier_call = cpu_percpu_callback,
    .priority = 100 /* highest priority */
};

static int __init percpu_presmp_init(void)
{
    register_cpu_notifier(&cpu_percpu_nfb);
    return 0;
}
presmp_initcall(percpu_presmp_init);

/* === END INLINED: percpu.c === */
/* === BEGIN INLINED: stop_machine.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * common/stop_machine.c
 *
 * Facilities to put whole machine in a safe 'stop' state
 *
 * Copyright 2005 Rusty Russell rusty@rustcorp.com.au IBM Corporation
 * Copyright 2008 Kevin Tian <kevin.tian@intel.com>, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <prtos_init.h>
#include <prtos_sched.h>
#include <prtos_spinlock.h>
#include <prtos_tasklet.h>
#include <prtos_stop_machine.h>
#include <prtos_errno.h>
#include <prtos_smp.h>
#include <prtos_cpu.h>
#include <asm_current.h>
#include <asm_processor.h>

enum stopmachine_state {
    STOPMACHINE_START,
    STOPMACHINE_PREPARE,
    STOPMACHINE_DISABLE_IRQ,
    STOPMACHINE_INVOKE,
    STOPMACHINE_EXIT
};

struct stopmachine_data {
    unsigned int nr_cpus;

    enum stopmachine_state state;
    atomic_t done;

    unsigned int fn_cpu;
    int fn_result;
    int (*fn)(void *data);
    void *fn_data;
};

static DEFINE_PER_CPU(struct tasklet, stopmachine_tasklet);
static struct stopmachine_data stopmachine_data;
static DEFINE_SPINLOCK(stopmachine_lock);

static void stopmachine_set_state(enum stopmachine_state state)
{
    atomic_set(&stopmachine_data.done, 0);
    smp_wmb();
    stopmachine_data.state = state;
}

static void stopmachine_wait_state(void)
{
    while ( atomic_read(&stopmachine_data.done) != stopmachine_data.nr_cpus )
        cpu_relax();
}

/*
 * Sync all processors and call a function on one or all of them.
 * As stop_machine_run() is using a tasklet for syncing the processors it is
 * mandatory to be called only on an idle vcpu, as otherwise active core
 * scheduling might hang.
 */
int stop_machine_run(int (*fn)(void *data), void *data, unsigned int cpu)
{
    unsigned int i, nr_cpus;
    unsigned int this = smp_processor_id();
    int ret;

    BUG_ON(!local_irq_is_enabled());
    BUG_ON(!is_idle_vcpu(current));

    /* cpu_online_map must not change. */
    if ( !get_cpu_maps() )
        return -EBUSY;

    nr_cpus = num_online_cpus();
    if ( cpu_online(this) )
        nr_cpus--;

    /* Must not spin here as the holder will expect us to be descheduled. */
    if ( !spin_trylock(&stopmachine_lock) )
    {
        put_cpu_maps();
        return -EBUSY;
    }

    stopmachine_data.fn = fn;
    stopmachine_data.fn_data = data;
    stopmachine_data.nr_cpus = nr_cpus;
    stopmachine_data.fn_cpu = cpu;
    stopmachine_data.fn_result = 0;
    atomic_set(&stopmachine_data.done, 0);
    stopmachine_data.state = STOPMACHINE_START;

    smp_wmb();

    for_each_online_cpu ( i )
        if ( i != this )
            tasklet_schedule_on_cpu(&per_cpu(stopmachine_tasklet, i), i);

    stopmachine_set_state(STOPMACHINE_PREPARE);
    stopmachine_wait_state();

    local_irq_disable();
    stopmachine_set_state(STOPMACHINE_DISABLE_IRQ);
    stopmachine_wait_state();
    spin_debug_disable();

    stopmachine_set_state(STOPMACHINE_INVOKE);
    if ( (cpu == this) || (cpu == NR_CPUS) )
    {
        ret = (*fn)(data);
        if ( ret )
            write_atomic(&stopmachine_data.fn_result, ret);
    }
    stopmachine_wait_state();
    ret = stopmachine_data.fn_result;

    spin_debug_enable();
    stopmachine_set_state(STOPMACHINE_EXIT);
    stopmachine_wait_state();
    local_irq_enable();

    spin_unlock(&stopmachine_lock);

    put_cpu_maps();

    return ret;
}

static void cf_check stopmachine_action(void *data)
{
    unsigned int cpu = (unsigned long)data;
    enum stopmachine_state state = STOPMACHINE_START;

    BUG_ON(cpu != smp_processor_id());

    smp_mb();

    while ( state != STOPMACHINE_EXIT )
    {
        while ( stopmachine_data.state == state )
            cpu_relax();

        state = stopmachine_data.state;
        switch ( state )
        {
        case STOPMACHINE_DISABLE_IRQ:
            local_irq_disable();
            break;
        case STOPMACHINE_INVOKE:
            if ( (stopmachine_data.fn_cpu == smp_processor_id()) ||
                 (stopmachine_data.fn_cpu == NR_CPUS) )
            {
                int ret = stopmachine_data.fn(stopmachine_data.fn_data);

                if ( ret )
                    write_atomic(&stopmachine_data.fn_result, ret);
            }
            break;
        default:
            break;
        }

        smp_mb();
        atomic_inc(&stopmachine_data.done);
    }

    local_irq_enable();
}

static int cf_check cpu_callback(
    struct notifier_block *nfb, unsigned long action, void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;

    if ( action == CPU_UP_PREPARE )
        tasklet_init(&per_cpu(stopmachine_tasklet, cpu),
                     stopmachine_action, hcpu);

    return NOTIFY_DONE;
}

static struct notifier_block cpu_nfb = {
    .notifier_call = cpu_callback
};

static int __init cf_check cpu_stopmachine_init(void)
{
    unsigned int cpu;
    for_each_online_cpu ( cpu )
    {
        void *hcpu = (void *)(long)cpu;
        cpu_callback(&cpu_nfb, CPU_UP_PREPARE, hcpu);
    }
    register_cpu_notifier(&cpu_nfb);
    return 0;
}
__initcall(cpu_stopmachine_init);

/* === END INLINED: stop_machine.c === */
