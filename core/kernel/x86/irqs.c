/*
 * FILE: irqs.c
 *
 * IRQS' code
 *
 * www.prtos.org
 */
#include <assert.h>
#include <bitwise.h>
#include <irqs.h>
#include <kdevice.h>
#include <kthread.h>
#include <physmm.h>
#include <processor.h>
#include <sched.h>
#include <stdc.h>
#include <arch/segments.h>

#ifdef CONFIG_SMI_DISABLE
#include <arch/io.h>
#include <arch/pci.h>
#include <arch/smi.h>
#endif
#ifdef CONFIG_APIC
#include <arch/apic.h>
#else
#include <arch/pic.h>
#endif

prtos_u32_t x86_hw_irqs_mask[CONFIG_NO_CPUS] = {[0 ...(CONFIG_NO_CPUS - 1)] = 0xffffffff};

#ifdef CONFIG_VERBOSE_TRAP
prtos_s8_t *trap_to_str[] = {
    __STR(DIVIDE_ERROR),                 // 0
    __STR(RESERVED_TRAP_1),              // 1
    __STR(NMI_INTERUPT),                 // 2
    __STR(BREAKPOINT),                   // 3
    __STR(OVERFLOW),                     // 4
    __STR(BOUND_RANGE_EXCEEDED),         // 5
    __STR(UNDEFINED_OPCODE),             // 6
    __STR(DEVICE_NOT_AVAILABLE),         // 7
    __STR(DOUBLE_FAULT),                 // 8
    __STR(COPROCESSOR_SEGMENT_OVERRUN),  // 9
    __STR(INVALID_TSS),                  // 10
    __STR(SEGMENT_NOT_PRESENT),          // 11
    __STR(STACK_SEGMENT_FAULT),          // 12
    __STR(GENERAL_PROTECTION),           // 13
    __STR(PAGE_FAULT),                   // 14
    __STR(RESERVED_TRAP_15),             // 15
    __STR(X87_FPU_ERROR),                // 16
    __STR(ALIGNMENT_CHECK),              // 17
    __STR(MACHINE_CHECK),                // 18
    __STR(SIMD_EXCEPTION),               // 19
};
#endif

#ifdef CONFIG_SMP
RESERVE_HWIRQ(HALT_ALL_IPI_IRQ);
RESERVE_HWIRQ(SCHED_PENDING_IPI_IRQ);

static void smp_halt_all_handle(cpu_ctxt_t *ctxt, void *data) {
    halt_system();
}

static void smp_sched_pending_ipi_handle(cpu_ctxt_t *ctxt, void *data) {
    set_sched_pending();
}
#endif

prtos_s32_t arch_trap_is_sys_ctxt(cpu_ctxt_t *ctxt) {
    return 1;
}

static inline void _set_irq_gate(prtos_s32_t e, void *hndl, prtos_u32_t dpl) {
    hyp_idt_table[e].seg_selector = CS_SEL;
    hyp_idt_table[e].offset15_0 = (prtos_address_t)hndl & 0xffff;
    hyp_idt_table[e].offset31_16 = ((prtos_address_t)hndl >> 16) & 0xffff;
    hyp_idt_table[e].access = 0x8e | (dpl & 0x3) << 5;
}

static inline void _set_trap_gate(prtos_s32_t e, void *hndl, prtos_u32_t dpl) {
    hyp_idt_table[e].seg_selector = CS_SEL;
    hyp_idt_table[e].offset15_0 = (prtos_address_t)hndl & 0xffff;
    hyp_idt_table[e].offset31_16 = ((prtos_address_t)hndl >> 16) & 0xffff;
    hyp_idt_table[e].access = 0x8f | (dpl & 0x3) << 5;
}

void setup_x86_idt(void) {
    extern void (*hyp_irq_handlers_table[0])(void);
    extern void (*hyp_trap_handlers_table[0])(void);
    extern void unexpected_irq(void);
    prtos_s32_t irq_nr;

    for (irq_nr = 0; irq_nr < CONFIG_NO_HWIRQS; irq_nr++) {
        ASSERT(hyp_irq_handlers_table[irq_nr]);
        _set_irq_gate(irq_nr + FIRST_EXTERNAL_VECTOR, hyp_irq_handlers_table[irq_nr], 0);
    }

    _set_trap_gate(0, hyp_trap_handlers_table[0], 0);
    _set_trap_gate(1, hyp_trap_handlers_table[1], 0);
    _set_irq_gate(2, hyp_trap_handlers_table[2], 0);

    _set_trap_gate(3, hyp_trap_handlers_table[3], 3);
    _set_trap_gate(4, hyp_trap_handlers_table[4], 3);
    _set_trap_gate(5, hyp_trap_handlers_table[5], 3);

    _set_trap_gate(6, hyp_trap_handlers_table[6], 0);
    _set_irq_gate(7, hyp_trap_handlers_table[7], 0);
    _set_trap_gate(8, hyp_trap_handlers_table[8], 0);
    _set_trap_gate(9, hyp_trap_handlers_table[9], 0);
    _set_trap_gate(10, hyp_trap_handlers_table[10], 0);
    _set_trap_gate(11, hyp_trap_handlers_table[11], 0);
    _set_trap_gate(12, hyp_trap_handlers_table[12], 0);
    _set_irq_gate(13, hyp_trap_handlers_table[13], 0);
    _set_irq_gate(14, hyp_trap_handlers_table[14], 0);
    _set_trap_gate(15, hyp_trap_handlers_table[15], 0);
    _set_trap_gate(16, hyp_trap_handlers_table[16], 0);
    _set_trap_gate(17, hyp_trap_handlers_table[17], 0);
    _set_trap_gate(18, hyp_trap_handlers_table[18], 0);
    _set_trap_gate(19, hyp_trap_handlers_table[19], 0);
}

static prtos_s32_t x86_trap_device_not_available(cpu_ctxt_t *ctxt, prtos_u16_t *hm_event) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_u32_t cr0;

    if (info && info->sched.current_kthread->ctrl.g) {
        cr0 = info->sched.current_kthread->ctrl.g->part_ctrl_table->arch.cr0;
        cr0 = (cr0 & ~_CR0_TS) | (save_cr0() & _CR0_TS);
        info->sched.current_kthread->ctrl.g->part_ctrl_table->arch.cr0 = cr0;
    }

    return 0;
}

static prtos_s32_t x86_trap_page_fault(cpu_ctxt_t *ctxt, prtos_u16_t *hm_event) {
    prtos_address_t fault_address = save_cr2();
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    if (info && info->sched.current_kthread->ctrl.g) info->sched.current_kthread->ctrl.g->part_ctrl_table->arch.cr2 = fault_address;
    if (fault_address >= CONFIG_PRTOS_OFFSET) {
        *hm_event = PRTOS_HM_EV_MEM_PROTECTION;
    }
    return 0;
}

void arch_setup_irqs(void) {
    set_trap_handler(PAGE_FAULT, x86_trap_page_fault);
    set_trap_handler(DEVICE_NOT_AVAILABLE, x86_trap_device_not_available);
#ifdef CONFIG_SMP
    set_irq_handler(HALT_ALL_IPI_IRQ, smp_halt_all_handle, 0);
    set_irq_handler(SCHED_PENDING_IPI_IRQ, smp_sched_pending_ipi_handle, 0);
#endif
#ifdef CONFIG_SMI_DISABLE
    smi_disable();
#endif
}

prtos_address_t irq_vector_to_address(prtos_s32_t vector) {
    return 0;
}

static inline prtos_s32_t test_sp(prtos_address_t *sp, prtos_u32_t size) {
    prtos_s32_t ret;
    set_wp();
    ret = asm_rw_check(((prtos_address_t)sp) - size, size, 1);
    clear_wp();
    return ret;
}

#define ERRCODE_TAB 0x27d00UL
void fix_stack(cpu_ctxt_t *ctxt, partition_control_table_t *part_ctrl_table, prtos_s32_t irq_nr, prtos_s32_t vector, prtos_s32_t trap) {
    prtos_word_t ip, iflags;
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct x86_gate *idt_entry;
    prtos_address_t *sp = 0;
    prtos_u16_t cs;

    if (vector >= part_ctrl_table->arch.max_idt_vec) {
        part_panic(ctxt, "error emulating IRQ (%d) bad vector (%d)", irq_nr, vector);
    }
    idt_entry = (struct x86_gate *)part_ctrl_table->arch.idtr.linear_base + vector;
    cs = idt_entry->seg_selector;
    ip = (idt_entry->offset31_16 << 16) | idt_entry->offset15_0;
    if ((ip >= CONFIG_PRTOS_OFFSET) || !(cs & 0x3)) {
        part_panic(ctxt, "error emulating IRQ (%d) CS:IP (0x%x:0x%x) invalid in IDT", irq_nr, cs, ip);
    }
    iflags = part_ctrl_table->iflags;
    if (!(idt_entry->access & 0x1)) {
        // Disable interruption if in a interrupt process
        part_ctrl_table->iflags &= ~_CPU_FLAG_IF;
    }

    if ((ctxt->cs & 0x3) == (cs & 0x3)) {
        // As priority level has not changes, no need to addjust stack regiser(.e.i: ss, esp)
        sp = (prtos_address_t *)ctxt->sp;
        if (test_sp(sp, 16) < 0) part_panic(ctxt, "error emulating IRQ (%d) bad stack pointer 0x%x", irq_nr, sp);
    } else {
        prtos_u16_t ss = 0;
        switch (cs & 0x3) {
            case 0x1:
                ss = info->sched.current_kthread->ctrl.g->karch.tss.t.ss1;
                sp = (prtos_address_t *)info->sched.current_kthread->ctrl.g->karch.tss.t.sp1;
                break;
            case 0x2:
                ss = info->sched.current_kthread->ctrl.g->karch.tss.t.ss2;
                sp = (prtos_address_t *)info->sched.current_kthread->ctrl.g->karch.tss.t.sp2;
                break;
            default:
                part_panic(ctxt, "error emulating IRQ (%d) CS (0x%x) corrupted", irq_nr, cs);
                break;
        }
        if (!ss || !sp) {
            part_panic(ctxt, "error emulating IRQ (%d) SS:ESP (0x%x:0x%x) invalid", irq_nr, ss, sp);
        }
        if (test_sp(sp, 24) < 0) part_panic(ctxt, "error emulating IRQ (%d) bad stack pointer 0x%x", irq_nr, sp);
        *(--sp) = ctxt->ss;
        *(--sp) = ctxt->sp;
        ctxt->ss = ss;
    }
    *(--sp) = (ctxt->flags & ~_CPU_FLAG_IF) | iflags;
    *(--sp) = ctxt->cs;
    *(--sp) = ctxt->ip;

    if (trap && ((1 << irq_nr) & ERRCODE_TAB)) *(--sp) = ctxt->err_code;

    ctxt->sp = (prtos_word_t)sp;
    ctxt->cs = cs;
    ctxt->ip = ip;
}

prtos_u32_t hw_irq_get_mask(prtos_s32_t e) {
    return x86_hw_irqs_mask[GET_CPU_ID()];
}
