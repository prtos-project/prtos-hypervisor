/*
 * FILE: gen_offsets.h
 *
 * ASM offsets, this file only can be included from asm-offset.c
 *
 * www.prtos.org
 */

#ifndef __GEN_OFFSETS_H_
#define _GEN_OFFSETS_H_
#ifndef _GENERATE_OFFSETS_
#error Do not include this file
#endif

#include <objects/commports.h>
#include <objects/console.h>
#include <objects/status.h>
#include <drivers/memblock.h>
#include <arch/atomic.h>
#include <kthread.h>
#include <sched.h>
#include <kdevice.h>
#include <logstream.h>
#include <physmm.h>
#include <local.h>

static inline void generate_offsets(void) {
    // local_processor_t
    DEFINE(local_sched, offsetof(local_processor_t, sched), );
    DEFINE(current_kthread, offsetof(local_processor_t, sched) + offsetof(local_sched_t, current_kthread), );

    // kthread_t
    DEFINE(ctrl, offsetof(kthread_t, ctrl), );

    // guest
    DEFINE(part_ctrl_table, offsetof(struct guest, part_ctrl_table), );

    // struct __kthread
    DEFINE(g, offsetof(struct __kthread, g), );
    DEFINE(kstack, offsetof(struct __kthread, kstack), );
    DEFINE(irq_cpu_ctxt, offsetof(struct __kthread, irq_cpu_ctxt), );

    // gctrl_t
    DEFINE(id, offsetof(partition_control_table_t, id), );
    //DEFINE(iflags, offsetof(partition_control_table_t, iflags), );
    // DEFINE(flags, offsetof(cpu_ctxt_t, flags), );
    // DEFINE(cs, offsetof(cpu_ctxt_t, cs), );
    // DEFINE(ax, offsetof(cpu_ctxt_t, ax), );
    // DEFINE(err_code, offsetof(cpu_ctxt_t, err_code), );
    // DEFINE(prev, offsetof(cpu_ctxt_t, prev), );
    // DEFINE(local_cpu_cpu_ctxt, offsetof(local_cpu_t, cpu_ctxt),);
    // DEFINE(local_time_flags, offsetof(local_time_t, flags),);
    // sizeof
    DEFINE2(kthread_t, sizeof(kthread_t), );
    DEFINE2(kthreadptr_t, sizeof(kthread_t *), );
    DEFINE2(partition_t, sizeof(partition_t), );
    DEFINE2(struct_guest, sizeof(struct guest), );
    DEFINE2(kdevice_t, sizeof(kdevice_t), );
    DEFINE2(struct_memblockdata, sizeof(struct mem_block_data), );
    DEFINE2(struct_console, sizeof(struct console), );
    DEFINE2(prtos_part_status_t, sizeof(prtos_part_status_t), );
    DEFINE2(struct_logstream, sizeof(struct log_stream), );
    DEFINE2(union_channel, sizeof(union channel), );
    DEFINE2(struct_port, sizeof(struct port), );
    DEFINE2(struct_msg, sizeof(struct msg), );
    DEFINE2(struct_physpageptr, sizeof(struct phys_page *), );
    DEFINE2(struct_physpage, sizeof(struct phys_page), );
    DEFINE2(struct_scheddata, sizeof(struct sched_data), );
    DEFINE2(local_time_t, sizeof(local_time_t), );
#ifdef CONFIG_SMP
    DEFINE2(local_processor_t, sizeof(local_processor_t), );
#endif
}

#endif
