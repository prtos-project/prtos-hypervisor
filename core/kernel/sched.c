/*
 * FILE: sched.c
 *
 * Scheduling related stuffs
 *
 * www.prtos.org
 */

#include <assert.h>
#include <boot.h>
#include <rsvmem.h>
#include <irqs.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>
#include <arch/asm.h>
#ifdef CONFIG_OBJ_STATUS_ACC
#include <objects/status.h>
#endif

#include <local.h>

partition_t *partition_table;
static struct sched_data *sched_data_table;

static const struct prtos_conf_sched_cyclic_plan idle_cyclic_plan_table = {
    .name_offset = 0,
    .id = 0,
    .major_frame = 0,
    .num_of_slots = 0,
    .slots_offset = 0,
};

prtos_s32_t switch_sched_plan(prtos_s32_t new_plan_id, prtos_s32_t *old_plan_id) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_s32_t e;

    *old_plan_id = -1;
    if (info->sched.data->plan.current) *old_plan_id = info->sched.data->plan.current->id;

    for (e = 0; e < prtos_conf_table.hpv.num_of_cpus; e++) {
        if (new_plan_id < prtos_conf_table.hpv.cpu_table[e].num_of_sched_cyclic_plans)
            local_processor_info[e].sched.data->plan.new =
                &prtos_conf_sched_cyclic_plan_table[prtos_conf_table.hpv.cpu_table[e].sched_cyclic_plans_offset + new_plan_id];
        else
            local_processor_info[e].sched.data->plan.new = &idle_cyclic_plan_table;
    }

#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_PLAN_SWITCH_REQ, 1, (prtos_word_t *)&new_plan_id);
#endif
    return 0;
}

inline void make_plan_switch(prtos_time_t current_time, struct sched_data *data) {
    extern void idle_task(void);
#ifdef CONFIG_AUDIT_EVENTS
    prtos_word_t plan_ids[2];
#endif
    if (data->plan.current != data->plan.new) {
        data->plan.prev = data->plan.current;
        data->plan.current = data->plan.new;
        data->plan_switch_time = current_time;
        data->slot = -1;
        data->major_frame = 0;

        if (!data->plan.new->major_frame) idle_task();
#ifdef CONFIG_AUDIT_EVENTS
        plan_ids[0] = data->plan.current->id;
        plan_ids[1] = data->plan.new->id;
        raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_PLAN_SWITCH_DONE, 2, plan_ids);
#endif
    }
}

int is_kthread_ready(kthread_t *kthread) {
    if (kthread && !are_kthread_flags_set(kthread, KTHREAD_HALTED_F | KTHREAD_SUSPENDED_F) && are_kthread_flags_set(kthread, KTHREAD_READY_F)) {
        return 1;
    } else {
        return 0;
    }
}

static kthread_t *get_current_kthread(kthread_t *kthread) {
    if (is_kthread_ready(kthread)) {
        return kthread;  // kthread is eligible to run
    } else {
        return 0;  // idle kthread
    }
}

static kthread_t *get_ready_kthread(struct sched_data *cyclic) {
    const struct prtos_conf_sched_cyclic_plan *plan;
    prtos_time_t current_time = get_sys_clock_usec();
    kthread_t *new_kthread = 0;
    prtos_u32_t t, next_time;
    prtos_s32_t slot_table_entry;

    if (cyclic->next_act > current_time) return get_current_kthread(cyclic->kthread);

    plan = cyclic->plan.current;
    if (cyclic->major_frame <= current_time) {
        make_plan_switch(current_time, cyclic);
        plan = cyclic->plan.current;
        if (cyclic->slot >= 0) {
            while (cyclic->major_frame <= current_time) {
                cyclic->start_exec = cyclic->major_frame;
                cyclic->major_frame += plan->major_frame;
            }
#ifdef CONFIG_OBJ_STATUS_ACC
            system_status.current_maf++;
#endif
        } else {
            cyclic->start_exec = current_time;
            cyclic->major_frame = plan->major_frame + cyclic->start_exec;
        }

        cyclic->slot = 0;
    }

    t = current_time - cyclic->start_exec;
    next_time = plan->major_frame;

    // Calculate our next slot
    if (cyclic->slot >= plan->num_of_slots) goto out;  // getting idle

    while (t >= prtos_conf_sched_cyclic_slot_table[plan->slots_offset + cyclic->slot].end_exec) {
        cyclic->slot++;
        if (cyclic->slot >= plan->num_of_slots) goto out;  // getting idle kthread
    }
    slot_table_entry = plan->slots_offset + cyclic->slot;

    if (t >= prtos_conf_sched_cyclic_slot_table[slot_table_entry].start_exec) {
        ASSERT((prtos_conf_sched_cyclic_slot_table[slot_table_entry].partition_id >= 0) &&
               (prtos_conf_sched_cyclic_slot_table[slot_table_entry].partition_id < prtos_conf_table.num_of_partitions));
        ASSERT(partition_table[prtos_conf_sched_cyclic_slot_table[slot_table_entry].partition_id]
                   .kthread[prtos_conf_sched_cyclic_slot_table[slot_table_entry].vcpu_id]);
        new_kthread =
            partition_table[prtos_conf_sched_cyclic_slot_table[slot_table_entry].partition_id].kthread[prtos_conf_sched_cyclic_slot_table[slot_table_entry].vcpu_id];

        if (is_kthread_ready(new_kthread)) {
            next_time = prtos_conf_sched_cyclic_slot_table[slot_table_entry].end_exec;
        } else {
            new_kthread = 0;
            if ((cyclic->slot + 1) < plan->num_of_slots) next_time = prtos_conf_sched_cyclic_slot_table[slot_table_entry + 1].start_exec;
        }
    } else {
        next_time = prtos_conf_sched_cyclic_slot_table[slot_table_entry].start_exec;
    }

out:
    ASSERT(cyclic->next_act < (next_time + cyclic->start_exec));
    cyclic->next_act = next_time + cyclic->start_exec;
    arm_ktimer(&cyclic->ktimer, cyclic->next_act, 0);
    slot_table_entry = plan->slots_offset + cyclic->slot;
#if PRTOS_VERBOSE
    if (new_kthread) {
        kprintf("[%d:%d:%d] current_time %lld -> start_exec %lld end_exec %lld\n", GET_CPU_ID(), cyclic->slot,
                prtos_conf_sched_cyclic_slot_table[slot_table_entry].partition_id, current_time,
                prtos_conf_sched_cyclic_slot_table[slot_table_entry].start_exec + cyclic->start_exec,
                prtos_conf_sched_cyclic_slot_table[slot_table_entry].end_exec + cyclic->start_exec);
    } else {
        kprintf("[%d] IDLE: %lld\n", GET_CPU_ID(), current_time);
    }
#endif

    if (new_kthread && new_kthread->ctrl.g) {
        new_kthread->ctrl.g->op_mode = PRTOS_OPMODE_NORMAL;
        new_kthread->ctrl.g->part_ctrl_table->cyclic_sched_info.num_of_slots = cyclic->slot;
        new_kthread->ctrl.g->part_ctrl_table->cyclic_sched_info.id = prtos_conf_sched_cyclic_slot_table[slot_table_entry].id;
        new_kthread->ctrl.g->part_ctrl_table->cyclic_sched_info.slot_duration =
            prtos_conf_sched_cyclic_slot_table[slot_table_entry].end_exec - prtos_conf_sched_cyclic_slot_table[slot_table_entry].start_exec;
        set_ext_irq_pending(new_kthread, PRTOS_VT_EXT_CYCLIC_SLOT_START);
    }

    cyclic->kthread = new_kthread;

    return new_kthread;
}

void __VBOOT init_sched(void) {
    extern void setup_kthreads(void);

    setup_kthreads();
    GET_MEMZ(partition_table, sizeof(partition_t) * prtos_conf_table.num_of_partitions);
    GET_MEMZ(sched_data_table, sizeof(struct sched_data) * prtos_conf_table.hpv.num_of_cpus);
}

void __VBOOT init_sched_local(kthread_t *idle) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    init_idle(idle, GET_CPU_ID());
    info->sched.current_kthread = info->sched.idle_kthread = idle;
    info->sched.data = &sched_data_table[GET_CPU_ID()];
    memset(info->sched.data, 0, sizeof(struct sched_data));
    init_ktimer(GET_CPU_ID(), &info->sched.data->ktimer, (void (*)(struct ktimer *, void *))set_sched_pending, NULL, NULL);
    info->sched.data->slot = -1;
    info->sched.data->plan.new = &prtos_conf_sched_cyclic_plan_table[prtos_conf_table.hpv.cpu_table[GET_CPU_ID()].sched_cyclic_plans_offset];
    info->sched.data->plan.current = 0;
    info->sched.data->plan.prev = 0;
}

void set_sched_pending(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    info->cpu.irq_nesting_counter |= SCHED_PENDING;
}

void schedule(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_word_t hw_flags;
    kthread_t *new_kthread;

    CHECK_KTHR_SANITY(info->sched.current_kthread);
    if (!(info->sched.flags & LOCAL_SCHED_ENABLED)) {
        info->cpu.irq_nesting_counter &= ~(SCHED_PENDING);
        return;
    }

    hw_save_flags_cli(hw_flags);
    // When an interrupt is in-progress, the scheduler shouldn't be invoked
    if (info->cpu.irq_nesting_counter & IRQ_IN_PROGRESS) {
        info->cpu.irq_nesting_counter |= SCHED_PENDING;
        hw_restore_flags(hw_flags);
        return;
    }

    info->cpu.irq_nesting_counter &= (~SCHED_PENDING);
    if (!(new_kthread = get_ready_kthread(info->sched.data))) new_kthread = info->sched.idle_kthread;

    CHECK_KTHR_SANITY(new_kthread);
    if (new_kthread != info->sched.current_kthread) {
#ifdef CONFIG_AUDIT_EVENTS
        prtos_word_t audit_args;

        audit_args = (new_kthread != info->sched.idle_kthread) ? new_kthread->ctrl.g->id : -1;
        raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_CONTEXT_SWITCH, 1, &audit_args);
#endif
#if PRTOS_VERBOSE
        if (new_kthread->ctrl.g)
            kprintf("new_kthread: [%d:%d] 0x%x ", KID2PARTID(new_kthread->ctrl.g->id), KID2VCPUID(new_kthread->ctrl.g->id), new_kthread);
        else
            kprintf("new_kthread: idle ");

        if (info->sched.current_kthread->ctrl.g)
            kprintf("curK: [%d:%d] 0x%x\n", KID2PARTID(info->sched.current_kthread->ctrl.g->id), KID2VCPUID(info->sched.current_kthread->ctrl.g->id));
        else
            kprintf("curK: idle\n");
#endif

        switch_kthread_arch_pre(new_kthread, info->sched.current_kthread);
        if (info->sched.current_kthread->ctrl.g)  // not idle kthread
            stop_vclock(&info->sched.current_kthread->ctrl.g->vclock, &info->sched.current_kthread->ctrl.g->vtimer);

        if (new_kthread->ctrl.g) set_hw_timer(traverse_ktimer_queue(&new_kthread->ctrl.local_active_ktimers, get_sys_clock_usec()));

        info->sched.current_kthread->ctrl.irq_mask = hw_irq_get_mask();
        hw_irq_set_mask(new_kthread->ctrl.irq_mask);

        CONTEXT_SWITCH(new_kthread, &info->sched.current_kthread);

        if (info->sched.current_kthread->ctrl.g) {
            resume_vclock(&info->sched.current_kthread->ctrl.g->vclock, &info->sched.current_kthread->ctrl.g->vtimer);
        }

        switch_kthread_arch_post(info->sched.current_kthread);
    }
    hw_restore_flags(hw_flags);
}
