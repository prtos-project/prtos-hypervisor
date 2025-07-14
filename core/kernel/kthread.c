/*
 * FILE: kthread.c
 *
 * Kernel and Guest context
 *
 * www.prtos.org
 */

#include <assert.h>
#include <rsvmem.h>
#include <ktimer.h>
#include <kthread.h>
#include <physmm.h>
#include <sched.h>
#include <spinlock.h>
#include <stdc.h>
#include <virtmm.h>
#include <vmmap.h>
#include <prtosef.h>

#include <objects/trace.h>
#include <arch/asm.h>
#include <arch/prtos_def.h>

static prtos_s32_t num_of_vcpus;

static void kthread_timer_handle(ktimer_t *ktimer, void *args) {
    kthread_t *k = (kthread_t *)args;
    CHECK_KTHR_SANITY(k);
    set_ext_irq_pending(k, PRTOS_VT_EXT_HW_TIMER);
}

void setup_kthreads(void) {
    prtos_s32_t e;

    ASSERT(GET_CPU_ID() == 0);
    for (e = 0, num_of_vcpus = 0; e < prtos_conf_table.num_of_partitions; e++) num_of_vcpus += prtos_conf_partition_table[e].num_of_vcpus;
}

void init_idle(kthread_t *idle, prtos_s32_t cpu) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    idle->ctrl.magic1 = idle->ctrl.magic2 = KTHREAD_MAGIC;
    dyn_list_init(&idle->ctrl.local_active_ktimers);
    idle->ctrl.lock = SPINLOCK_INIT;
    idle->ctrl.irq_cpu_ctxt = 0;
    idle->ctrl.kstack = 0;
    idle->ctrl.g = 0;
    prtos_s32_t e;
    for (e = 0; e < HWIRQS_VECTOR_SIZE; e++) {
        idle->ctrl.irq_mask[e] = hw_irq_get_mask(e) | info->cpu.global_irq_mask[e];
    }
    set_kthread_flags(idle, KTHREAD_DCACHE_ENABLED_F | KTHREAD_ICACHE_ENABLED_F);
    set_kthread_flags(idle, KTHREAD_READY_F);
    clear_kthread_flags(idle, KTHREAD_HALTED_F);
}

void start_up_guest(prtos_address_t entry) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    kthread_t *k = info->sched.current_kthread;
    cpu_ctxt_t ctxt;

    kthread_arch_init(k);
    set_kthread_flags(info->sched.current_kthread, KTHREAD_DCACHE_ENABLED_F | KTHREAD_ICACHE_ENABLED_F);
    set_cache_state(DCACHE | ICACHE);
    resume_vclock(&k->ctrl.g->vclock, &k->ctrl.g->vtimer);
    switch_kthread_arch_post(k);

    // JMP_PARTITION must enable interrupts
    JMP_PARTITION(entry, k);

    get_cpu_ctxt(&ctxt);
    part_panic(&ctxt, __PRTOS_FILE__ ":%u:0x%x: executing unreachable code!", __LINE__, k);
}

static inline kthread_t *alloc_kthread(prtos_id_t id) {
    kthread_t *k;

    GET_MEMAZ(k, sizeof(kthread_t), ALIGNMENT);
    GET_MEMAZ(k->ctrl.g, sizeof(struct guest), ALIGNMENT);

    k->ctrl.magic1 = k->ctrl.magic2 = KTHREAD_MAGIC;
    k->ctrl.g->id = id;
    dyn_list_init(&k->ctrl.local_active_ktimers);
    k->ctrl.lock = SPINLOCK_INIT;
    return k;
}

partition_t *create_partition(struct prtos_conf_part *cfg) {
    prtos_u8_t *pct;
    prtos_u_size_t pct_size;
    prtos_u32_t local_irq_mask[HWIRQS_VECTOR_SIZE];
    partition_t *p;
    prtos_s32_t i;

    ASSERT((cfg->id >= 0) && (cfg->id < prtos_conf_table.num_of_partitions));
    p = &partition_table[cfg->id];
    GET_MEMAZ(p->kthread, cfg->num_of_vcpus * sizeof(kthread_t *), ALIGNMENT);
    p->cfg = cfg;

    pct_size = sizeof(partition_control_table_t) + sizeof(struct prtos_physical_mem_map) * cfg->num_of_physical_memory_areas +
               (cfg->num_of_ports >> PRTOS_LOG2_WORD_SZ);

    if (cfg->num_of_ports & ((1 << PRTOS_LOG2_WORD_SZ) - 1)) pct_size += sizeof(prtos_word_t);

    GET_MEMAZ(pct, pct_size * cfg->num_of_vcpus, PAGE_SIZE);
    p->pct_array_size = pct_size * cfg->num_of_vcpus;
    p->pct_array = (prtos_address_t)pct;
    for (i = 0; i < cfg->num_of_vcpus; i++) {
        kthread_t *k, *ck;
        prtos_s32_t e;
        prtos_u32_t cpu_id;
        p->kthread[i] = k = alloc_kthread(PART_VCPU_ID2KID(cfg->id, i));
        if (cfg->flags & PRTOS_PART_SYSTEM) {
            clear_kthread_flags(k, KTHREAD_NO_PARTITIONS_FIELD);
            set_kthread_flags(k, (prtos_conf_table.num_of_partitions << 16) & KTHREAD_NO_PARTITIONS_FIELD);
        }

        cpu_id = prtos_conf_vcpu_table[(cfg->id * prtos_conf_table.hpv.num_of_cpus) + i].cpu;

        if (cfg->flags & PRTOS_PART_FP) set_kthread_flags(k, KTHREAD_FP_F);

        ck = k;

        init_ktimer(cpu_id, &k->ctrl.g->ktimer, kthread_timer_handle, k, ck);
        init_vtimer(cpu_id, &k->ctrl.g->vtimer, k);

        set_kthread_flags(k, KTHREAD_HALTED_F);
        prtos_s32_t e_index;
        for (e_index = 0; e_index < HWIRQS_VECTOR_SIZE; e_index++) {
            local_irq_mask[e_index] = local_processor_info[cpu_id].cpu.global_irq_mask[e_index];
            for (e = 0; e < CONFIG_NO_HWIRQS; e++)
                if (prtos_conf_table.hpv.hw_irq_table[e].owner == cfg->id) local_irq_mask[e_index] &= ~(1 << e);
            k->ctrl.irq_mask[e_index] = local_irq_mask[e_index];
        }
        k->ctrl.irq_cpu_ctxt = 0;
        k->ctrl.g->part_ctrl_table = (partition_control_table_t *)(pct + pct_size * i);
        k->ctrl.g->part_ctrl_table->part_ctrl_table_size = pct_size;
        setup_kthread_arch(k);
    }

    return p;
}

static inline void setup_pct(partition_control_table_t *part_ctrl_table, kthread_t *k, struct prtos_conf_part *cfg) {
    struct prtos_physical_mem_map *mem_map = (struct prtos_physical_mem_map *)((prtos_address_t)part_ctrl_table + sizeof(partition_control_table_t));
    struct prtos_conf_memory_area *prtos_conf_mem_area;
    prtos_word_t *comm_port_bitmap;
    prtos_s32_t e, reset_counter;
    partition_t *p = get_partition(k);

    comm_port_bitmap = (prtos_word_t *)((prtos_address_t)mem_map + sizeof(struct prtos_physical_mem_map) * cfg->num_of_physical_memory_areas);

    reset_counter = part_ctrl_table->reset_counter;
    memset(part_ctrl_table, 0, sizeof(partition_control_table_t));
    part_ctrl_table->reset_counter = reset_counter;
    part_ctrl_table->magic = KTHREAD_MAGIC;
    part_ctrl_table->prtos_version = PRTOS_VERSION;
    part_ctrl_table->prtos_abi_version = PRTOS_SET_VERSION(PRTOS_ABI_VERSION, PRTOS_ABI_SUBVERSION, PRTOS_ABI_REVISION);
    part_ctrl_table->prtos_api_version = PRTOS_SET_VERSION(PRTOS_API_VERSION, PRTOS_API_SUBVERSION, PRTOS_API_REVISION);
    part_ctrl_table->cpu_khz = cpu_khz;
    part_ctrl_table->hw_irqs = cfg->hw_irqs;
    part_ctrl_table->flags = k->ctrl.flags;
    part_ctrl_table->id = k->ctrl.g->id;
    part_ctrl_table->num_of_vcpus = cfg->num_of_vcpus;
    strncpy(part_ctrl_table->name, &prtos_conf_string_tab[cfg->name_offset], CONFIG_ID_STRING_LENGTH);
    part_ctrl_table->hw_irqs_mask |= ~0;
    part_ctrl_table->ext_irqs_to_mask |= ~0;
    part_ctrl_table->image_start = p->image_start;

    init_part_ctrl_table_irqs(&part_ctrl_table->iflags);
    part_ctrl_table->num_of_physical_mem_areas = cfg->num_of_physical_memory_areas;
    part_ctrl_table->part_ctrl_table_size =
        sizeof(partition_control_table_t) + part_ctrl_table->num_of_physical_mem_areas * sizeof(struct prtos_physical_mem_map);
    for (e = 0; e < cfg->num_of_physical_memory_areas; e++) {
        prtos_conf_mem_area = &prtos_conf_phys_mem_area_table[e + cfg->physical_memory_areas_offset];
        if (prtos_conf_mem_area->flags & PRTOS_MEM_AREA_TAGGED)
            strncpy(mem_map[e].name, &prtos_conf_string_tab[prtos_conf_mem_area->name_offset], CONFIG_ID_STRING_LENGTH);
        mem_map[e].start_addr = prtos_conf_mem_area->start_addr;
        mem_map[e].mapped_at = prtos_conf_mem_area->mapped_at;
        mem_map[e].size = prtos_conf_mem_area->size;
        mem_map[e].flags = prtos_conf_mem_area->flags;
    }

    prtos_clear_bitmap(comm_port_bitmap, cfg->num_of_ports);
    part_ctrl_table->num_of_comm_ports = cfg->num_of_ports;

    setup_pct_mm(part_ctrl_table, k);
    setup_pct_arch(part_ctrl_table, k);
}

void reset_kthread(kthread_t *k, prtos_address_t ptd_level_1, prtos_address_t entry_point, prtos_u32_t status) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct phys_page *page = NULL;
    prtos_address_t vptd;

    vptd = enable_by_pass_mmu(ptd_level_1, get_partition(k), &page);

    setup_ptd_level_1_table((prtos_word_t *)vptd, k);

    disable_by_pass_mmu(page);

    setup_pct(k->ctrl.g->part_ctrl_table, k, get_partition(k)->cfg);
    k->ctrl.g->part_ctrl_table->reset_counter++;
    k->ctrl.g->part_ctrl_table->reset_status = status;
    k->ctrl.g->karch.ptd_level_1 = ptd_level_1;
    k->ctrl.g->part_ctrl_table->arch._ARCH_PTDL1_REG = ptd_level_1;

    set_kthread_flags(k, KTHREAD_READY_F);
    clear_kthread_flags(k, KTHREAD_HALTED_F);
#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_VCPU_RESET, 1, &k->ctrl.g->id);
#endif
    if (k != info->sched.current_kthread) {
        setup_kstack(k, start_up_guest, entry_point);
#ifdef CONFIG_SMP
        if (k->ctrl.g) {
            prtos_u8_t cpu = prtos_conf_vcpu_table[(KID2PARTID(k->ctrl.g->id) * prtos_conf_table.hpv.num_of_cpus) + KID2VCPUID(k->ctrl.g->id)].cpu;
            if (cpu != GET_CPU_ID())
                send_ipi(cpu, NO_SHORTHAND_IPI, SCHED_PENDING_IPI_VECTOR);
            else
                schedule();
        }
#else
        schedule();
#endif
    } else {
        load_part_page_table(k);
        start_up_guest(entry_point);
    }
}

prtos_s32_t reset_partition(partition_t *p, prtos_u32_t cold, prtos_u32_t status) {
    extern void reset_part_ports(partition_t * p);
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct prtos_conf_boot_part *prtos_conf_boot_part;
    struct prtos_image_hdr *prtos_image_hdr;
    prtos_address_t ptd_level_1;

    // All the vCPUs are halted
    HALT_PARTITION(p->cfg->id);
    prtos_conf_boot_part = &prtos_conf_boot_partition_table[p->cfg->id];

    // Is partition image valid?
    if (!(prtos_conf_boot_part->flags & PRTOS_PART_BOOT)) return -1;

    if (!phys_mm_find_area(prtos_conf_boot_part->hdr_phys_addr, sizeof(struct prtos_image_hdr), p, 0)) return -1;

    prtos_image_hdr = (struct prtos_image_hdr *)prtos_conf_boot_part->hdr_phys_addr;

    if ((read_by_pass_mmu_word(&prtos_image_hdr->start_signature) != PRTOS_EXEC_PARTITION_MAGIC) ||
        (read_by_pass_mmu_word(&prtos_image_hdr->end_signature) != PRTOS_EXEC_PARTITION_MAGIC))
        return -1;

    if (read_by_pass_mmu_word(&prtos_image_hdr->compilation_prtos_abi_version) !=
        PRTOS_SET_VERSION(PRTOS_ABI_VERSION, PRTOS_ABI_SUBVERSION, PRTOS_ABI_REVISION))
        return -1;

    if (read_by_pass_mmu_word(&prtos_image_hdr->compilation_prtos_api_version) !=
        PRTOS_SET_VERSION(PRTOS_API_VERSION, PRTOS_API_SUBVERSION, PRTOS_API_REVISION))
        return -1;

    if (info->sched.current_kthread == p->kthread[0]) load_hyp_page_table();

    phys_mm_reset_part(p);
    ptd_level_1 = setup_page_table(p, read_by_pass_mmu_word(&prtos_image_hdr->page_table), read_by_pass_mmu_word(&prtos_image_hdr->page_table_size));
    if (ptd_level_1 == ~0) return -1;

    switch (cold) {
        case PRTOS_WARM_RESET: /*WARM RESET*/
            p->kthread[0]->ctrl.g->op_mode = PRTOS_OPMODE_WARM_RESET;
            p->op_mode = PRTOS_OPMODE_WARM_RESET;
            reset_part_ports(p);
            reset_kthread(p->kthread[0], ptd_level_1, prtos_conf_boot_partition_table[p->cfg->id].entry_point, status);
            break;
        case PRTOS_COLD_RESET: /*COLD RESET -> Partition Loader*/
            p->kthread[0]->ctrl.g->op_mode = PRTOS_OPMODE_COLD_RESET;
            p->op_mode = PRTOS_OPMODE_COLD_RESET;
            p->kthread[0]->ctrl.g->part_ctrl_table->reset_counter = -1;
            reset_part_ports(p);
            reset_kthread(p->kthread[0], ptd_level_1, PRTOS_PCTRLTAB_ADDR - 256 * 1024, status);
            break;
        case 2: /*COLD RESET -> used at boot time*/
            p->kthread[0]->ctrl.g->op_mode = PRTOS_OPMODE_COLD_RESET;
            p->op_mode = PRTOS_OPMODE_COLD_RESET;
            p->kthread[0]->ctrl.g->part_ctrl_table->reset_counter = -1;
            reset_part_ports(p);
            reset_kthread(p->kthread[0], ptd_level_1, prtos_conf_boot_partition_table[p->cfg->id].entry_point, status);
            break;
    }

    if (info->sched.current_kthread == p->kthread[0]) load_part_page_table(p->kthread[0]);

    return 0;
}
