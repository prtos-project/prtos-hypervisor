/*
 * FILE: kthread.c
 *
 * Kernel, Guest context (ARCH dependent part) for amd64
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
    if (current->ctrl.g) {
        current->ctrl.g->karch.ptd_level_1 = save_cr3();
        current->ctrl.g->karch.cr0 = save_cr0();
    }

    if (new->ctrl.g) {
#ifdef CONFIG_VCPU_MIGRATION
        new->ctrl.g->karch.gdt_table[PERCPU_SEL >> 3] = gdt_table[GDT_ENTRY(GET_CPU_ID(), PERCPU_SEL)];
#endif
        load_gdt(new->ctrl.g->karch.gdtr);
        load_idt(new->ctrl.g->karch.idtr);
        /* CR3 is NOT loaded here. It will be loaded in start_up_guest
         * (via load_part_page_table/switch_kthread_arch_post) or upon
         * resuming a partition that was previously interrupted.
         * Loading CR3 during CONTEXT_SWITCH would require the partition's
         * page tables to perfectly mirror kernel mappings, which is fragile
         * on amd64's 4-level paging. Instead, we keep the kernel CR3
         * active during the context switch. */
        tss_clear_busy(&new->ctrl.g->karch.gdtr, TSS_SEL);
        load_tr(TSS_SEL);
    } else {
        load_hyp_page_table();
    }
    load_cr0(_CR0_PE | _CR0_PG);
}

void switch_kthread_arch_post(kthread_t *current) {
    if (current->ctrl.g) {
        load_cr0(current->ctrl.g->karch.cr0);
        load_part_page_table(current);
    }
}

/*
 * Trampoline bridging CONTEXT_SWITCH → start_up_guest for amd64 calling convention.
 * CONTEXT_SWITCH does 'ret' which pops _start_up_trampoline from the stack.
 * Stack at that point: [rsp]=start_up_guest addr, [rsp+8]=entry_point.
 * We load %rdi (first arg per SysV ABI) = entry_point, then jump to start_up_guest.
 */
__asm__(".text\n"
        ".globl _start_up_trampoline\n"
        "_start_up_trampoline:\n"
        "    popq %rax\n"     /* rax = start_up_guest address */
        "    popq %rdi\n"     /* rdi = entry_point (first argument) */
        "    jmp *%rax\n");   /* tail-call start_up_guest(entry_point) */

extern void _start_up_trampoline(void);

extern void setup_kstack(kthread_t *k, void *start_up, prtos_address_t entry_point) {
    k->ctrl.kstack = (prtos_u64_t *)((prtos_address_t)(unsigned long)&k->kstack[CONFIG_KSTACK_SIZE]);
    *--(k->ctrl.kstack) = entry_point;
    *--(k->ctrl.kstack) = (prtos_address_t)(unsigned long)start_up;
    *--(k->ctrl.kstack) = (prtos_address_t)(unsigned long)_start_up_trampoline;
}

void kthread_arch_init(kthread_t *k) {
    k->ctrl.g->karch.tss.t.rsp0 = (prtos_u64_t)(unsigned long)&k->kstack[CONFIG_KSTACK_SIZE];
    k->ctrl.g->karch.gdt_table[PERCPU_SEL >> 3] = gdt_table[GDT_ENTRY(GET_CPU_ID(), PERCPU_SEL)];
    set_wp();
}

void setup_kthread_arch(kthread_t *k) {
    partition_t *p = get_partition(k);

    ASSERT(k->ctrl.g);
    ASSERT(p);
    memcpy(k->ctrl.g->karch.gdt_table, gdt_table, sizeof(struct x86_desc) * (PRTOS_GDT_ENTRIES + CONFIG_PARTITION_NO_GDT_ENTRIES));

    k->ctrl.g->karch.gdtr.limit = (sizeof(struct x86_desc) * (CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES)) - 1;
    k->ctrl.g->karch.gdtr.linear_base = (prtos_u64_t)(unsigned long)k->ctrl.g->karch.gdt_table;

    /* Set TSS.RSP0 early so exceptions during the first CONTEXT_SWITCH
     * have a valid kernel stack. kthread_arch_init will refresh it. */
    k->ctrl.g->karch.tss.t.rsp0 = (prtos_u64_t)(unsigned long)&k->kstack[CONFIG_KSTACK_SIZE];

    k->ctrl.g->karch.cr0 = _CR0_PE | _CR0_PG;

    memcpy(k->ctrl.g->karch.hyp_idt_table, hyp_idt_table, sizeof(struct x86_gate) * IDT_ENTRIES);

    k->ctrl.g->karch.idtr.limit = (sizeof(struct x86_gate) * IDT_ENTRIES) - 1;
    k->ctrl.g->karch.idtr.linear_base = (prtos_u64_t)(unsigned long)k->ctrl.g->karch.hyp_idt_table;

    if (p->cfg->num_of_io_ports > 0) {
        memcpy(k->ctrl.g->karch.tss.io_map, prtos_conf_io_port_table[p->cfg->io_ports_offset].map, 2048 * sizeof(prtos_u32_t));
        enable_tss_io_map(&k->ctrl.g->karch.tss);
    } else
        disable_tss_io_map(&k->ctrl.g->karch.tss);
    load_tss_desc(&k->ctrl.g->karch.gdt_table[TSS_SEL >> 3], &k->ctrl.g->karch.tss);
}

void setup_pct_arch(partition_control_table_t *part_ctrl_table, kthread_t *k) {
    prtos_s32_t e;

    part_ctrl_table->arch.cr3 = k->ctrl.g->karch.ptd_level_1;
    part_ctrl_table->arch.cr0 = _CR0_PE | _CR0_PG;

    for (e = 0; e < NO_TRAPS; e++) k->ctrl.g->part_ctrl_table->trap_to_vector[e] = e;

    for (e = 0; e < CONFIG_NO_HWIRQS; e++) k->ctrl.g->part_ctrl_table->hw_irq_to_vector[e] = e + FIRST_EXTERNAL_VECTOR;

    for (e = 0; e < PRTOS_VT_EXT_MAX; e++) k->ctrl.g->part_ctrl_table->ext_irq_to_vector[e] = 0x90 + e;
}
