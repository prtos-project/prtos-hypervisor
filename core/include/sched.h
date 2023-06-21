/*
 * FILE: sched.h
 *
 * Scheduling related stuffs
 *
 * www.prtos.org
 */

#ifndef _PRTOS_SCHED_H_
#define _PRTOS_SCHED_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <processor.h>
#include <kthread.h>
#include <smp.h>
#include <objects/status.h>
#include <local.h>

extern void init_sched(void);
extern void init_sched_local(kthread_t *idle);
extern void schedule(void);
extern void set_sched_pending(void);
extern prtos_s32_t switch_sched_plan(prtos_s32_t new_plan_id, prtos_s32_t *old_plan_id);

static inline void sched_yield(local_processor_t *info, kthread_t *k) {
    info->sched.data->kthread = k;
    schedule();
}

static inline void do_preemption(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    hw_cli();
    hw_irq_set_mask(info->cpu.global_irq_mask);
    hw_sti();

    do_nop();

    hw_cli();
    hw_irq_set_mask(info->sched.current_kthread->ctrl.irqMask);
}

static inline void preemption_on(void) {
#ifdef CONFIG_VOLUNTARY_PREEMPTION
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    info->sched.current_kthread->ctrl.irqMask = hw_irq_get_mask();
    hw_irq_set_mask(info->cpu.global_irq_mask);
    hw_sti();
#endif
}

static inline void preemption_off(void) {
#ifdef CONFIG_VOLUNTARY_PREEMPTION
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    hw_cli();
    hw_irq_set_mask(info->sched.current_kthread->ctrl.irqMask);
#endif
}

#include <audit.h>

static inline void SUSPEND_VCPU(prtos_id_t part_id, prtos_id_t vcpu_id) {
#ifdef CONFIG_AUDIT_EVENTS
    prtos_word_t arg = PART_VCPU_ID2KID(part_id, vcpu_id);
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_VCPU_SUSPEND, 1, &arg);
#endif
    set_kthread_flags(part_table[part_id].kthread[vcpu_id], KTHREAD_SUSPENDED_F);
}

static inline void RESUME_VCPU(prtos_id_t part_id, prtos_id_t vcpu_id) {
#ifdef CONFIG_AUDIT_EVENTS
    prtos_word_t arg = PART_VCPU_ID2KID(part_id, vcpu_id);
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_VCPU_RESUME, 1, &arg);
#endif
    clear_kthread_flags(part_table[part_id].kthread[vcpu_id], KTHREAD_SUSPENDED_F);
}

static inline void HALT_VCPU(prtos_id_t part_id, prtos_id_t vcpu_id) {
#ifdef CONFIG_AUDIT_EVENTS
    prtos_word_t arg = PART_VCPU_ID2KID(part_id, vcpu_id);
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_VCPU_HALT, 1, &arg);
#endif
    set_kthread_flags(part_table[part_id].kthread[vcpu_id], KTHREAD_HALTED_F);
    part_table[part_id].kthread[vcpu_id]->ctrl.g->op_mode = PRTOS_OPMODE_IDLE;
}

static inline void SUSPEND_PARTITION(prtos_id_t id) {
    prtos_s32_t e;
#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_PART_SUSPEND, 1, (prtos_word_t *)&id);
#endif
    for (e = 0; e < part_table[id].cfg->num_of_vcpus; e++) set_kthread_flags(part_table[id].kthread[e], KTHREAD_SUSPENDED_F);
}

static inline void RESUME_PARTITION(prtos_id_t id) {
    prtos_s32_t e;
#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_PART_RESUME, 1, (prtos_word_t *)&id);
#endif
    for (e = 0; e < part_table[id].cfg->num_of_vcpus; e++) clear_kthread_flags(part_table[id].kthread[e], KTHREAD_SUSPENDED_F);
}

static inline void SHUTDOWN_PARTITION(prtos_id_t id) {
#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_PART_SHUTDOWN, 1, (prtos_word_t *)&id);
#endif
    set_part_ext_irq_pending(&part_table[id], PRTOS_VT_EXT_SHUTDOWN);
}
/*
static inline void IDLE_PARTITION(prtos_id_t id) {
    prtos_s32_t e;
#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_PART_IDLE, 1, (prtos_word_t *)&id);
#endif
    for (e=0; e<part_table[id].cfg->num_of_vcpus; e++)
        clear_kthread_flags(part_table[id].kthread[e], KTHREAD_READY_F);
}
*/
static inline void HALT_PARTITION(prtos_id_t id) {
    prtos_s32_t e;
#ifdef CONFIG_AUDIT_EVENTS
    raise_audit_event(TRACE_SCHED_MODULE, AUDIT_SCHED_PART_HALT, 1, (prtos_word_t *)&id);
#endif
    for (e = 0; e < part_table[id].cfg->num_of_vcpus; e++) {
        set_kthread_flags(part_table[id].kthread[e], KTHREAD_HALTED_F);
        part_table[id].kthread[e]->ctrl.g->op_mode = PRTOS_OPMODE_IDLE;
    }
    part_table[id].op_mode = PRTOS_OPMODE_IDLE;
}

extern inline void make_plan_switch(prtos_time_t current_time, struct sched_data *cyclic);

#endif
