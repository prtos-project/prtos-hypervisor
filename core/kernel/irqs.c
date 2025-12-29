/*
 * FILE: irqs.c
 *
 * Independent part of interrupt handling
 *
 * www.prtos.org
 */

#include <audit.h>
#include <bitwise.h>
#include <boot.h>
#include <irqs.h>
#include <kthread.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>
#include <processor.h>
#include <objects/hm.h>
#ifdef CONFIG_OBJ_STATUS_ACC
#include <objects/status.h>
#endif

// Definitions
struct irq_table_entry irq_handler_table[CONFIG_NO_HWIRQS];
trap_handler_t trap_handler_table[NO_TRAPS];
hw_irq_ctrl_t hw_irq_ctrl[CONFIG_NO_HWIRQS];

void do_unrecover_exception(cpu_ctxt_t *ctxt);

void default_irq_handler(cpu_ctxt_t *ctxt, void *data) {
#ifndef CONFIG_AARCH64  // FIXME: here is the WA for build pass
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_hm_log_t hm_log;

    memset(&hm_log, 0, sizeof(prtos_hm_log_t));
    hm_log.op_code_lo |= PRTOS_HM_EV_UNEXPECTED_TRAP << HMLOG_OPCODE_EVENT_BIT;

    hm_log.op_code_lo |= (ctxt->irq_nr & HMLOG_OPCODE_EVENT_MASK) << (HMLOG_OPCODE_EVENT_BIT + 8);

    if (info->sched.current_kthread != info->sched.idle_kthread) {
        hm_log.op_code_lo |= KID2PARTID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_PARTID_BIT;
        hm_log.op_code_lo |= KID2VCPUID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_VCPUID_BIT;
    } else {
        hm_log.op_code_lo |= (~0 & HMLOG_OPCODE_PARTID_MASK) << HMLOG_OPCODE_PARTID_BIT;
        hm_log.op_code_lo |= (~0 & HMLOG_OPCODE_VCPUID_MASK) << HMLOG_OPCODE_VCPUID_BIT;
    }

    hm_log.op_code_hi |= HMLOG_OPCODE_SYS_MASK;

    cpu_ctxt_to_hm_cpu_ctxt(ctxt, &hm_log.cpu_ctxt);
    hm_log.op_code_hi |= HMLOG_OPCODE_VALID_CPUCTXT_MASK;
    hm_raise_event(&hm_log);

    kprintf("Unexpected irq %d\n", ctxt->irq_nr);
#endif
}

static void trigger_irq_handler(cpu_ctxt_t *ctxt, void *data) {
#ifndef CONFIG_AARCH64  // FIXME: here is the WA for build pass
    prtos_id_t part_id;
    part_id = prtos_conf_table.hpv.hw_irq_table[ctxt->irq_nr].owner;

    set_part_hw_irq_pending(&partition_table[part_id], ctxt->irq_nr);
#endif
}

void set_trap_pending(cpu_ctxt_t *ctxt) {
#ifndef CONFIG_AARCH64  // FIXME: here is the WA for build pass
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    ASSERT(!are_kthread_flags_set(info->sched.current_kthread, KTHREAD_TRAP_PENDING_F));
    set_kthread_flags(info->sched.current_kthread, KTHREAD_TRAP_PENDING_F);
#endif
}

static inline prtos_address_t is_in_part_exception_table(prtos_address_t addr) {
#ifndef CONFIG_AARCH64  // FIXME: here is the WA for build pass
    extern struct exception_table {
        prtos_address_t a;
        prtos_address_t b;
    } exception_table[];
    struct exception_table *exception_table_ptr = exception_table;
    prtos_s32_t e;

    for (e = 0; exception_table_ptr[e].a; e++) {
        if (addr == exception_table_ptr[e].a) return exception_table_ptr[e].b;
    }
#endif
    return 0;
}

void do_hyp_trap(cpu_ctxt_t *ctxt) {
#ifndef CONFIG_AARCH64  // FIXME: here is the WA for build pass
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_hm_log_t hm_log;
    prtos_s32_t action;
    prtos_u16_t hm_event;
    prtos_word_t pc;
#ifdef CONFIG_AUDIT_EVENTS
    prtos_word_t audit_args[2];
#endif

    ASSERT(ctxt->irq_nr < NO_TRAPS);
    if ((pc = is_in_part_exception_table(GET_CTXT_PC(ctxt)))) {
        SET_CTXT_PC(ctxt, pc);
        return;
    }

#ifdef CONFIG_AUDIT_EVENTS
    if (is_audit_event_masked(TRACE_IRQ_MODULE)) {
        audit_args[0] = ctxt->irq_nr;
        audit_args[1] = GET_CTXT_PC(ctxt);
        raise_audit_event(TRACE_IRQ_MODULE, AUDIT_TRAP_RAISED, 2, audit_args);
    }
#endif

    hm_event = ctxt->irq_nr + PRTOS_HM_MAX_GENERIC_EVENTS;

    if (trap_handler_table[ctxt->irq_nr])
        if (trap_handler_table[ctxt->irq_nr](ctxt, &hm_event)) return;

    if (info->sched.current_kthread->ctrl.g) {
        if (!are_part_ctrl_table_traps_set(info->sched.current_kthread->ctrl.g->part_ctrl_table->iflags)) do_unrecover_exception(ctxt);
    }

    /*Fast propagation of partition events not logged.*/
    if (!is_hpv_irq_ctxt(ctxt)) {
        ASSERT(hm_event < PRTOS_HM_MAX_EVENTS);
        if ((get_partition(info->sched.current_kthread)->cfg->hm_table[hm_event].action == PRTOS_HM_AC_PROPAGATE) &&
            (!get_partition(info->sched.current_kthread)->cfg->hm_table[hm_event].log)) {
#ifdef CONFIG_VERBOSE_TRAP
            kprintf("[TRAP] %s(0x%x)\n", trap_to_str[ctxt->irq_nr], ctxt->irq_nr);
            dump_state(ctxt);
#endif
            set_trap_pending(ctxt);
            return;
        }
    }

    memset(&hm_log, 0, sizeof(prtos_hm_log_t));
    hm_log.op_code_lo |= hm_event << HMLOG_OPCODE_EVENT_BIT;

    if (info->sched.current_kthread != info->sched.idle_kthread) {
        hm_log.op_code_lo |= KID2PARTID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_PARTID_BIT;
        hm_log.op_code_lo |= KID2VCPUID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_VCPUID_BIT;
    }

    if (is_hpv_irq_ctxt(ctxt)) {
        if (arch_trap_is_sys_ctxt(ctxt)) hm_log.op_code_hi |= HMLOG_OPCODE_SYS_MASK;
    }

    cpu_ctxt_to_hm_cpu_ctxt(ctxt, &hm_log.cpu_ctxt);
    hm_log.op_code_hi |= HMLOG_OPCODE_VALID_CPUCTXT_MASK;
#ifdef CONFIG_VERBOSE_TRAP
    kprintf("[TRAP] %s(0x%x)\n", trap_to_str[ctxt->irq_nr], ctxt->irq_nr);
    dump_state(ctxt);
#endif
    action = hm_raise_event(&hm_log);
    if (is_hpv_irq_ctxt(ctxt) && ((hm_log.op_code_hi & HMLOG_OPCODE_SYS_MASK) != HMLOG_OPCODE_SYS_MASK)) part_panic(ctxt, "Partition in unrecoverable state\n");
    if (!is_hpv_irq_ctxt(ctxt)) {
        if (action) set_trap_pending(ctxt);
    } else
        system_panic(ctxt, "Unexpected/unhandled trap - TRAP: 0x%x ERROR CODE: 0x%x\n",
                     info->sched.current_kthread->ctrl.g->part_ctrl_table->trap_to_vector[ctxt->irq_nr], GET_ECODE(ctxt));
#endif
}

void do_unrecover_exception(cpu_ctxt_t *ctxt) {
#ifndef CONFIG_AARCH64  // FIXME: here is the WA for build pass
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_hm_log_t hm_log;

    memset(&hm_log, 0, sizeof(prtos_hm_log_t));
    hm_log.op_code_lo |= PRTOS_HM_EV_SYSTEM_ERROR << HMLOG_OPCODE_EVENT_BIT;

    if (info->sched.current_kthread != info->sched.idle_kthread) {
        hm_log.op_code_lo |= KID2PARTID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_PARTID_BIT;
        hm_log.op_code_lo |= KID2VCPUID(info->sched.current_kthread->ctrl.g->id) << HMLOG_OPCODE_VCPUID_BIT;
    }

    if (is_hpv_irq_ctxt(ctxt)) {
        hm_log.op_code_hi |= HMLOG_OPCODE_SYS_MASK;
    }

    cpu_ctxt_to_hm_cpu_ctxt(ctxt, &hm_log.cpu_ctxt);
    hm_log.op_code_hi |= HMLOG_OPCODE_VALID_CPUCTXT_MASK;
    dump_state(ctxt);
    hm_raise_event(&hm_log);

    part_panic(ctxt, "Partition unrecoverable error : 0x%x\n", ctxt->irq_nr);
#endif
}

void do_hyp_irq(cpu_ctxt_t *ctxt) {
#ifndef CONFIG_AARCH64  // FIXME: here is the WA for build pass
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    ASSERT(!hw_is_sti());
#ifdef CONFIG_AUDIT_EVENTS
    if (is_audit_event_masked(TRACE_IRQ_MODULE)) raise_audit_event(TRACE_IRQ_MODULE, AUDIT_IRQ_RAISED, 1, &ctxt->irq_nr);
#endif
#ifdef CONFIG_OBJ_STATUS_ACC
    system_status.num_of_irqs++;
#endif
    info->cpu.irq_nesting_counter++;
    hw_ack_irq(ctxt->irq_nr);
    if (irq_handler_table[ctxt->irq_nr].handler)
        (*(irq_handler_table[ctxt->irq_nr].handler))(ctxt, irq_handler_table[ctxt->irq_nr].data);
    else
        default_irq_handler(ctxt, 0);
#ifndef CONFIG_MASKING_VT_HW_IRQS
    hw_end_irq(ctxt->irq_nr);
#endif
    info->cpu.irq_nesting_counter--;
    do {
        schedule();
    } while (info->cpu.irq_nesting_counter == SCHED_PENDING);
    ASSERT(!hw_is_sti());
    ASSERT(!(info->cpu.irq_nesting_counter & SCHED_PENDING));
#endif
}

void __VBOOT setup_irqs(void) {
    prtos_s32_t irq_nr;
#ifndef CONFIG_AARCH64  // FIXME: here is the WA for build pass
    for (irq_nr = 0; irq_nr < CONFIG_NO_HWIRQS; irq_nr++) {
        if (prtos_conf_table.hpv.hw_irq_table[irq_nr].owner != PRTOS_IRQ_NO_OWNER) {
            irq_handler_table[irq_nr] = (struct irq_table_entry){
                .handler = trigger_irq_handler,
                .data = 0,
            };
        } else {
            irq_handler_table[irq_nr] = (struct irq_table_entry){
                .handler = default_irq_handler,
                .data = 0,
            };
        }
    }

    for (irq_nr = 0; irq_nr < NO_TRAPS; irq_nr++) trap_handler_table[irq_nr] = 0;

    arch_setup_irqs();
#endif
}

irq_handler_t set_irq_handler(prtos_s32_t irq, irq_handler_t irq_handler, void *data) {
    irq_handler_t old_handler = irq_handler_table[irq].handler;

    if (irq_handler) {
        irq_handler_table[irq] = (struct irq_table_entry){
            .handler = irq_handler,
            .data = data,
        };
    } else
        irq_handler_table[irq] = (struct irq_table_entry){
            .handler = default_irq_handler,
            .data = 0,
        };
    return old_handler;
}

trap_handler_t set_trap_handler(prtos_s32_t trap, trap_handler_t trap_handler) {
    trap_handler_t old_handler = trap_handler_table[trap];

    trap_handler_table[trap] = trap_handler;
    return old_handler;
}

static inline prtos_s32_t are_hw_irqs_pending(partition_control_table_t *part_ctrl_table) {
    prtos_s32_t entry_irq;

    entry_irq = part_ctrl_table->hw_irqs_pend & ~part_ctrl_table->hw_irqs_mask;
    if (entry_irq) {
#ifdef CONFIG_HWIRQ_PRIO_FBS
        entry_irq = _ffs(entry_irq);
#else
        entry_irq = _fls(entry_irq);
#endif
        ASSERT(entry_irq >= 0 && entry_irq < CONFIG_NO_HWIRQS);
        return entry_irq;
    }

    return -1;
}

static inline prtos_s32_t are_ext_irqs_pending(partition_control_table_t *part_ctrl_table) {
    prtos_s32_t entry_irq;

    entry_irq = part_ctrl_table->ext_irqs_pend & ~part_ctrl_table->ext_irqs_to_mask;
    if (entry_irq) {
#ifdef CONFIG_HWIRQ_PRIO_FBS
        entry_irq = _ffs(entry_irq);
#else
        entry_irq = _fls(entry_irq);
#endif
        return entry_irq;
    }

    return -1;
}

static inline prtos_s32_t are_ext_traps_pending(partition_control_table_t *part_ctrl_table) {
    prtos_s32_t entry_irq;

    entry_irq = part_ctrl_table->ext_irqs_pend & PRTOS_EXT_TRAPS;
    if (entry_irq) {
#ifdef CONFIG_HWIRQ_PRIO_FBS
        entry_irq = _ffs(entry_irq);
#else
        entry_irq = _fls(entry_irq);
#endif
        return entry_irq;
    }

    return -1;
}

#ifdef CONFIG_AUDIT_EVENTS
#define RAISE_PENDIRQ_AUDIT_EVENT(pi)                                      \
    do {                                                                   \
        if (is_audit_event_masked(TRACE_IRQ_MODULE)) {                     \
            prtos_word_t _tmp = (pi);                                      \
            raise_audit_event(TRACE_IRQ_MODULE, AUDIT_IRQ_EMUL, 1, &_tmp); \
        }                                                                  \
    } while (0)
#else
#define RAISE_PENDIRQ_AUDIT_EVENT(pi)
#endif

prtos_s32_t raise_pend_irqs(cpu_ctxt_t *ctxt) {
#ifndef CONFIG_AARCH64  // FIXME: here is the WA for build pass
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    partition_control_table_t *part_ctrl_table;
    prtos_s32_t entry_irq, emul;

    if (!info->sched.current_kthread->ctrl.g || is_hpv_irq_ctxt(ctxt)) return ~0;

    // Software trap
    if (info->sched.current_kthread->ctrl.g->sw_trap & 0x1) {
        emul = info->sched.current_kthread->ctrl.g->sw_trap >> 1;
        info->sched.current_kthread->ctrl.g->sw_trap = 0;
        RAISE_PENDIRQ_AUDIT_EVENT(emul);
        return irq_vector_to_address(emul);
    }

    part_ctrl_table = info->sched.current_kthread->ctrl.g->part_ctrl_table;
    // 1) Check pending traps
    if (are_kthread_flags_set(info->sched.current_kthread, KTHREAD_TRAP_PENDING_F)) {
        clear_kthread_flags(info->sched.current_kthread, KTHREAD_TRAP_PENDING_F);
        disable_part_ctrl_table_irqs(&part_ctrl_table->iflags);
        emul = arch_emul_trap_irq(ctxt, part_ctrl_table, ctxt->irq_nr);
        RAISE_PENDIRQ_AUDIT_EVENT(emul);
        return irq_vector_to_address(emul);
    }

    // 2) check pending extended trap
    if ((entry_irq = are_ext_traps_pending(part_ctrl_table)) > -1) {
        part_ctrl_table->ext_irqs_pend &= ~(1 << entry_irq);
        disable_part_ctrl_table_irqs(&part_ctrl_table->iflags);
        emul = arch_emul_ext_irq(ctxt, part_ctrl_table, entry_irq);
        RAISE_PENDIRQ_AUDIT_EVENT(emul);
        return irq_vector_to_address(emul);
    }

    // At this moment irq flags must be set
    if (!are_part_ctrl_table_irqs_set(part_ctrl_table->iflags)) return ~0;

    // 3) Check pending hwirqs
    if ((entry_irq = are_hw_irqs_pending(part_ctrl_table)) > -1) {
        part_ctrl_table->hw_irqs_pend &= ~(1 << entry_irq);
        mask_part_ctrl_table_irq(&part_ctrl_table->hw_irqs_mask, (1 << entry_irq));
        disable_part_ctrl_table_irqs(&part_ctrl_table->iflags);
        emul = arch_emul_hw_irq(ctxt, part_ctrl_table, entry_irq);
        RAISE_PENDIRQ_AUDIT_EVENT(emul);
        return irq_vector_to_address(emul);
    }

    // 4) Check pending extirqs
    if ((entry_irq = are_ext_irqs_pending(part_ctrl_table)) > -1) {
        part_ctrl_table->ext_irqs_pend &= ~(1 << entry_irq);
        mask_part_ctrl_table_irq(&part_ctrl_table->ext_irqs_to_mask, (1 << entry_irq));
        disable_part_ctrl_table_irqs(&part_ctrl_table->iflags);
        emul = arch_emul_ext_irq(ctxt, part_ctrl_table, entry_irq);
        RAISE_PENDIRQ_AUDIT_EVENT(emul);
        return irq_vector_to_address(emul);
    }
#endif
    // No emulation required
    return ~0;
}
