/*
 * FILE: kthread.c
 *
 * Kernel, Guest context (ARCH dependent part)
 *
 * www.prtos.org
 */

#include <assert.h>
#include <rsvmem.h>
#include <gaccess.h>
#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>
#include <vmmap.h>
#include <arch/segments.h>
#include <arch/prtos_def.h>

void switch_kthread_arch_pre(kthread_t *new, kthread_t *current) {
#ifdef CONFIG_AARCH64
    if (!new->ctrl.g) {
        /* Switching to idle kthread: disable stage-2 MMU to prevent
         * stale VTTBR_EL2 from a previous partition causing translation faults. */
        asm volatile("msr vttbr_el2, xzr\n\t"
                     "mrs x10, hcr_el2\n\t"
                     "bic x10, x10, #1\n\t" /* clear VM bit */
                     "msr hcr_el2, x10\n\t"
                     "isb" ::
                         : "x10", "memory");
    }
#endif
}

void switch_kthread_arch_post(kthread_t *current) {
    // if (current->ctrl.g) {
    //     if (!(current->ctrl.g->karch.cr0 & _CR0_EM)) restore_fpu_state(current->ctrl.g->karch.fp_ctxt);
    //     load_cr0(current->ctrl.g->karch.cr0);
    // }
}

extern void kthread_startup_wrapper(void);

extern void setup_kstack(kthread_t *k, void *start_up, prtos_address_t entry_point) {
    prtos_u64_t *sp = (prtos_u64_t *)(&k->kstack[CONFIG_KSTACK_SIZE]);
    /* Build a stack frame matching the CONTEXT_SWITCH restore sequence:
     * ldp x19, x20 / ldp x21, x22 / ldp x23, x24 /
     * ldp x25, x26 / ldp x27, x28 / ldp x29(fp), x30(lr)
     * x19 = start_up, x20 = entry_point, x30 = kthread_startup_wrapper
     */
    *(--sp) = (prtos_u64_t)kthread_startup_wrapper; /* lr (x30) */
    *(--sp) = 0ULL;                                 /* fp (x29) */
    *(--sp) = 0ULL;
    *(--sp) = 0ULL; /* x28, x27 */
    *(--sp) = 0ULL;
    *(--sp) = 0ULL; /* x26, x25 */
    *(--sp) = 0ULL;
    *(--sp) = 0ULL; /* x24, x23 */
    *(--sp) = 0ULL;
    *(--sp) = 0ULL;                     /* x22, x21 */
    *(--sp) = (prtos_u64_t)entry_point; /* x20 */
    *(--sp) = (prtos_u64_t)start_up;    /* x19 */
    k->ctrl.kstack = (prtos_address_t *)sp;
}

void kthread_arch_init(kthread_t *k) {
    // if (are_kthread_flags_set(k, KTHREAD_FP_F)) {
    //     load_cr0(save_cr0() & (~(_CR0_EM | _CR0_TS)));
    //     FNINIT();
    //     save_fpu_state(k->ctrl.g->karch.fp_ctxt);
    // }

    // k->ctrl.g->karch.tss.t.ss0 = DS_SEL;
    // k->ctrl.g->karch.tss.t.sp0 = (prtos_address_t)&k->kstack[CONFIG_KSTACK_SIZE];
    // k->ctrl.g->karch.gdt_table[PERCPU_SEL >> 3] = gdt_table[GDT_ENTRY(GET_CPU_ID(), PERCPU_SEL)];
    // set_wp();
}

void setup_kthread_arch(kthread_t *k) {
    // partition_t *p = get_partition(k);

    // ASSERT(k->ctrl.g);
    // ASSERT(p);
    // memcpy(k->ctrl.g->karch.gdt_table, gdt_table, sizeof(struct x86_desc) * (PRTOS_GDT_ENTRIES + CONFIG_PARTITION_NO_GDT_ENTRIES));

    // k->ctrl.g->karch.gdtr.limit = (sizeof(struct x86_desc) * (CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES)) - 1;
    // k->ctrl.g->karch.gdtr.linear_base = (prtos_address_t)k->ctrl.g->karch.gdt_table;

    // k->ctrl.g->karch.cr0 = _CR0_PE | _CR0_PG;
    // if (!are_kthread_flags_set(k, KTHREAD_FP_F)) {
    //     k->ctrl.g->karch.cr0 |= _CR0_EM;
    // }

    // memcpy(k->ctrl.g->karch.hyp_idt_table, hyp_idt_table, sizeof(struct x86_gate) * IDT_ENTRIES);

    // k->ctrl.g->karch.idtr.limit = (sizeof(struct x86_gate) * IDT_ENTRIES) - 1;
    // k->ctrl.g->karch.idtr.linear_base = (prtos_address_t)k->ctrl.g->karch.hyp_idt_table;

    // if (p->cfg->num_of_io_ports > 0) {
    //     memcpy(k->ctrl.g->karch.tss.io_map, prtos_conf_io_port_table[p->cfg->io_ports_offset].map, 2048 * sizeof(prtos_u32_t));
    //     enable_tss_io_map(&k->ctrl.g->karch.tss);
    // } else
    //     disable_tss_io_map(&k->ctrl.g->karch.tss);
    // load_tss_desc(&k->ctrl.g->karch.gdt_table[TSS_SEL >> 3], &k->ctrl.g->karch.tss);
}

void setup_pct_arch(partition_control_table_t *part_ctrl_table, kthread_t *k) {
    // prtos_s32_t e;

    // part_ctrl_table->arch.cr3 = k->ctrl.g->karch.ptd_level_1;
    // part_ctrl_table->arch.cr0 = _CR0_PE | _CR0_PG;
    // if (!are_kthread_flags_set(k, KTHREAD_FP_F)) {
    //     part_ctrl_table->arch.cr0 |= _CR0_EM;
    // }

    // for (e = 0; e < NO_TRAPS; e++) k->ctrl.g->part_ctrl_table->trap_to_vector[e] = e;

    // for (e = 0; e < CONFIG_NO_HWIRQS; e++) k->ctrl.g->part_ctrl_table->hw_irq_to_vector[e] = e + FIRST_EXTERNAL_VECTOR;

    // for (e = 0; e < PRTOS_VT_EXT_MAX; e++) k->ctrl.g->part_ctrl_table->ext_irq_to_vector[e] = 0x90 + e;
}
