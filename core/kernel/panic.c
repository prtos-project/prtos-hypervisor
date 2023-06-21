/*
 * FILE: panic.c
 *
 * Code executed in a panic situation
 *
 * www.prtos.org
 */

#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>
#include <objects/hm.h>

void system_panic(cpu_ctxt_t *ctxt, prtos_s8_t *fmt, ...) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_hm_log_t hm_log;
    va_list vl;
    ASSERT(fmt);

    hw_cli();

    kprintf("System FATAL ERROR:\n");
    va_start(vl, fmt);
    vprintf(fmt, vl);
    va_end(vl);
    kprintf("\n");
    if (info && info->sched.current_kthread && ctxt) dump_state(ctxt);
    /*
      #ifdef CONFIG_DEBUG
      else {
      prtos_word_t ebp=save_bp();
      stack_backtrace(ebp);
      }
      #endif
    */
    hm_log.op_code_hi = 0;
    hm_log.op_code_lo = 0;
    if (info && (info->sched.current_kthread != info->sched.idle_kthread)) {
        hm_log.op_code_lo |= KID2PARTID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_PARTID_BIT;
        hm_log.op_code_lo |= KID2VCPUID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_VCPUID_BIT;
    }

    hm_log.op_code_lo |= PRTOS_HM_EV_INTERNAL_ERROR << HMLOG_OPCODE_EVENT_BIT;
    hm_log.op_code_hi |= HMLOG_OPCODE_SYS_MASK;

    if (info && info->sched.current_kthread && ctxt) {
        hm_log.op_code_hi |= HMLOG_OPCODE_VALID_CPUCTXT_MASK;
        cpu_ctxt_to_hm_cpu_ctxt(ctxt, &hm_log.cpu_ctxt);
    } else
        memset(hm_log.payload, 0, PRTOS_HMLOG_PAYLOAD_LENGTH * sizeof(prtos_word_t));

    hm_raise_event(&hm_log);

    smp_halt_all();
    halt_system();
}

void part_panic(cpu_ctxt_t *ctxt, prtos_s8_t *fmt, ...) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_hm_log_t hm_log;
    va_list vl;
    ASSERT(fmt);

    kprintf("Partition PANIC [");

    if (info->sched.current_kthread->ctrl.g)
        kprintf("0x%x:id(%d)]:\n", info->sched.current_kthread, get_partition(info->sched.current_kthread)->cfg->id);
    else
        kprintf("0x%x:IDLE]:\n", info->sched.current_kthread);

    va_start(vl, fmt);
    vprintf(fmt, vl);
    va_end(vl);
    kprintf("\n");
    if (ctxt) dump_state(ctxt);
    /*
    #ifdef CONFIG_DEBUG
        else {
            prtos_word_t ebp=save_bp();
            stack_backtrace(ebp);
        }
    #endif
    */
    if (info->sched.current_kthread == info->sched.idle_kthread) halt_system();

    hm_log.op_code_hi = 0;
    hm_log.op_code_lo = 0;

    if (info->sched.current_kthread != info->sched.idle_kthread) {
        hm_log.op_code_lo |= KID2PARTID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_PARTID_BIT;
        hm_log.op_code_lo |= KID2VCPUID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_VCPUID_BIT;
    }

    hm_log.op_code_lo |= PRTOS_HM_EV_INTERNAL_ERROR << HMLOG_OPCODE_EVENT_BIT;

    if (ctxt) {
        hm_log.op_code_hi |= HMLOG_OPCODE_VALID_CPUCTXT_MASK;
        cpu_ctxt_to_hm_cpu_ctxt(ctxt, &hm_log.cpu_ctxt);
    } else
        memset(hm_log.payload, 0, PRTOS_HMLOG_PAYLOAD_LENGTH * sizeof(prtos_word_t));
    hm_raise_event(&hm_log);

    // Finish current kthread
    set_kthread_flags(info->sched.current_kthread, KTHREAD_HALTED_F);
    if (info->sched.current_kthread == info->sched.idle_kthread) system_panic(ctxt, "Idle thread triggered a PANIC???");
    schedule();
    system_panic(ctxt, "[PANIC] This line should not be reached!!!");
}
