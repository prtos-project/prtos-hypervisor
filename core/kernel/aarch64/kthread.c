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
//     if (current->ctrl.g) {
//         current->ctrl.g->karch.ptd_level_1 = save_cr3();
//         current->ctrl.g->karch.cr0 = save_cr0();
//         if (!(current->ctrl.g->karch.cr0 & _CR0_EM)) {
//             if (save_cr0() & _CR0_TS) {
//                 CLTS();
//             }
//             save_fpu_state(current->ctrl.g->karch.fp_ctxt);
//         }
//     }

//     if (new->ctrl.g) {
// #ifdef CONFIG_VCPU_MIGRATION
//         new->ctrl.g->karch.gdt_table[PERCPU_SEL >> 3] = gdt_table[GDT_ENTRY(GET_CPU_ID(), PERCPU_SEL)];
// #endif
//         load_gdt(new->ctrl.g->karch.gdtr);
//         load_idt(new->ctrl.g->karch.idtr);
//         if (new->ctrl.g->karch.ptd_level_1) {
//             load_cr3(new->ctrl.g->karch.ptd_level_1);
//         }
//         tss_clear_busy(&new->ctrl.g->karch.gdtr, TSS_SEL);
//         load_tr(TSS_SEL);
//     } else {
//         load_hyp_page_table();
//     }
//     load_cr0(_CR0_PE | _CR0_PG);
}

void switch_kthread_arch_post(kthread_t *current) {
    // if (current->ctrl.g) {
    //     if (!(current->ctrl.g->karch.cr0 & _CR0_EM)) restore_fpu_state(current->ctrl.g->karch.fp_ctxt);
    //     load_cr0(current->ctrl.g->karch.cr0);
    // }
}

extern void setup_kstack(kthread_t *k, void *start_up, prtos_address_t entry_point) {
    // k->ctrl.kstack = (prtos_u32_t *)&k->kstack[CONFIG_KSTACK_SIZE];
    // *--(k->ctrl.kstack) = entry_point;
    // *--(k->ctrl.kstack) = 0;
    // *--(k->ctrl.kstack) = (prtos_address_t)start_up;
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
