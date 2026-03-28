/*
 * FILE: irqs.c
 *
 * RISC-V 64-bit IRQ handling
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

prtos_u32_t x86_hw_irqs_mask[CONFIG_NO_CPUS] = {[0 ...(CONFIG_NO_CPUS - 1)] = 0xffffffff};

#ifdef CONFIG_VERBOSE_TRAP
prtos_s8_t *trap_to_str[] = {
    [RISCV64_ILLEGAL_INSTR]      = "ILLEGAL_INSTR",
    [RISCV64_INSTR_ACCESS_FAULT] = "INSTR_ACCESS_FAULT",
    [RISCV64_LOAD_ACCESS_FAULT]  = "LOAD_ACCESS_FAULT",
    [RISCV64_STORE_ACCESS_FAULT] = "STORE_ACCESS_FAULT",
    [RISCV64_INSTR_PAGE_FAULT]   = "INSTR_PAGE_FAULT",
    [RISCV64_LOAD_PAGE_FAULT]    = "LOAD_PAGE_FAULT",
    [RISCV64_STORE_PAGE_FAULT]   = "STORE_PAGE_FAULT",
    [RISCV64_INSTR_MISALIGNED]   = "INSTR_MISALIGNED",
    [RISCV64_LOAD_MISALIGNED]    = "LOAD_MISALIGNED",
};
#endif

prtos_s32_t arch_trap_is_sys_ctxt(cpu_ctxt_t *ctxt) {
    return 1;
}

prtos_s32_t is_hpv_irq_ctxt(cpu_ctxt_t *ctxt) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    if (info->sched.current_kthread != info->sched.idle_kthread &&
        info->sched.current_kthread->ctrl.g)
        return 0;
    return 1;
}

/* IPI handler - triggers scheduler on target CPU */
static void riscv64_ipi_handler(cpu_ctxt_t *ctxt, void *data) {
    set_sched_pending();
}

/* Timer IRQ handler - triggers scheduler */
static void riscv64_timer_irq_handler(cpu_ctxt_t *ctxt, void *data) {
    set_sched_pending();
}

void arch_setup_irqs(void) {
    set_irq_handler(1, riscv64_ipi_handler, 0);    /* IRQ 1 = S-mode software interrupt (IPI) */
    set_irq_handler(5, riscv64_timer_irq_handler, 0);  /* IRQ 5 = S-mode timer */
}

prtos_address_t irq_vector_to_address(prtos_s32_t vector) {
    return 0;
}

/* Guest regs pointer for virtual IRQ injection */
extern struct cpu_user_regs *prtos_current_guest_regs_percpu[];

void fix_stack(cpu_ctxt_t *ctxt, partition_control_table_t *part_ctrl_table, prtos_s32_t irq_nr, prtos_s32_t vector, prtos_s32_t trap) {
    struct cpu_user_regs *regs = prtos_current_guest_regs_percpu[GET_CPU_ID()];

    if (!regs) return;
    if (!part_ctrl_table->arch.trap_entry) return;
    if (part_ctrl_table->arch.irq_vector) return;  /* already delivering */

    part_ctrl_table->arch.irq_saved_pc = regs->sepc;
    part_ctrl_table->arch.irq_saved_sstatus = regs->sstatus;
    part_ctrl_table->arch.irq_saved_a0 = regs->a0;
    part_ctrl_table->arch.irq_vector = (prtos_u32_t)vector;
    regs->sepc = part_ctrl_table->arch.trap_entry;
}

prtos_u32_t hw_irq_get_mask(prtos_s32_t e) {
    return 0;
}

void hw_irq_set_mask(prtos_s32_t e, prtos_u32_t mask) {
}
