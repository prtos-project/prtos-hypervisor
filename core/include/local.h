/*
 * FILE: sched.h
 *
 * proccessor per-stuffs
 *
 * www.prtos.org
 */

#ifndef _PRTOS_LOCAL_H_
#define _PRTOS_LOCAL_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <kthread.h>

typedef struct {
    struct local_cpu {
        prtos_u32_t flags;
#define CPU_SLOT_ENABLED (1 << 0)
#define BSP_FLAG (1 << 1)
        volatile prtos_u32_t irq_nesting_counter;
        prtos_u32_t global_irq_mask[HWIRQS_VECTOR_SIZE];
    } cpu;

    struct local_processor_sched {
        kthread_t *idle_kthread;
        kthread_t *current_kthread;
        kthread_t *fpu_owner;

        struct sched_data {
            ktimer_t ktimer;
            struct {
                const struct prtos_conf_sched_cyclic_plan *current;
                const struct prtos_conf_sched_cyclic_plan *new;
                const struct prtos_conf_sched_cyclic_plan *prev;
            } plan;
            prtos_s32_t slot;  // next slot to be processed
            prtos_time_t major_frame;
            prtos_time_t start_exec;
            prtos_time_t plan_switch_time;
            prtos_time_t next_act;
            kthread_t *kthread;
        } * data;

        prtos_u32_t flags;
#define LOCAL_SCHED_ENABLED 0x1
    } sched;

    struct local_time {
        prtos_u32_t flags;
#define NEXT_ACT_IS_VALID 0x1
        hw_timer_t *sys_hw_timer;
        prtos_time_t next_act;
        struct dyn_list global_active_ktimers;
    } time;

} local_processor_t;

typedef struct local_processor_sched local_sched_t;
typedef struct local_cpu local_cpu_t;
typedef struct local_time local_time_t;

extern local_processor_t local_processor_info[];

// Processor id is missed
#ifdef CONFIG_SMP
#define GET_LOCAL_PROCESSOR() (&local_processor_info[GET_CPU_ID()])
#else
#define GET_LOCAL_PROCESSOR() local_processor_info
#endif

#endif
