/*
 * FILE: hypercalls.c
 *
 * prtos's hypercalls
 *
 * www.prtos.org
 */

#include <assert.h>
#include <gaccess.h>
#include <guest.h>
#include <hypercalls.h>
#include <physmm.h>
#include <sched.h>
#include <stdc.h>

#define EMPTY_SEG 0
#define CODE_DATA_SEG 1
#define TSS_SEG 2
#define LDT_SEG 3

static inline prtos_s32_t is_gdt_desc_valid(struct x86_desc *desc, prtos_u32_t *type) {
    prtos_address_t base, limit;

    if (!(desc->low & X86DESC_LOW_P)) {
        *type = CODE_DATA_SEG;
        return 1;
    }
    *type = EMPTY_SEG;
    limit = get_desc_limit(desc);
    base = (prtos_u32_t)get_desc_base(desc);
    if ((limit + base) > CONFIG_PRTOS_OFFSET) {
        PWARN("[%d] GDT desc (0x%x:0x%x) limit too large\n", KID2PARTID(GET_LOCAL_PROCESSOR()->sched.current_kthread->ctrl.g->id), desc->high, desc->low);
        return 0;
    }
    if (desc->low & X86DESC_LOW_S) { /* Code/Data segment */
        *type = CODE_DATA_SEG;
        if (!((desc->low >> X86DESC_LOW_DPL_POS) & 0x3)) {
            PWARN("DESCR (%x:%x) bad permissions\n", desc->high, desc->low);
            return 0;
        }
        desc->low |= (1 << X86DESC_LOW_TYPE_POS);
    } else { /* System segment */
        switch ((desc->low >> X86DESC_LOW_TYPE_POS) & 0xF) {
            case 0x9:
            case 0xb:
                *type = TSS_SEG;
                break;
            default:
                return 0;
        }

        return 1;
    }
    return 1;
}

__hypercall prtos_s32_t x86_load_cr0_sys(prtos_word_t val) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    if ((val & (_CR0_PG | _CR0_PE)) != (_CR0_PG | _CR0_PE)) return PRTOS_OP_NOT_ALLOWED;
    if (!are_kthread_flags_set(info->sched.current_kthread, KTHREAD_FP_F)) val |= _CR0_EM;
    info->sched.current_kthread->ctrl.g->part_ctrl_table->arch.cr0 = val | _CR0_ET;
    val &= ~(_CR0_WP | _CR0_CD | _CR0_NW);
    info->sched.current_kthread->ctrl.g->karch.cr0 = val;
    load_cr0(val);

    return PRTOS_OK;
}

__hypercall prtos_s32_t x86_load_cr3_sys(prtos_word_t val) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct phys_page *new_cr3_page, *old_cr3_page;
    prtos_word_t old_cr3;

    old_cr3 = save_cr3();
    if (old_cr3 == val) {
        flush_tlb();
    } else {
        if (!(old_cr3_page = pmm_find_page(old_cr3, get_partition(info->sched.current_kthread), 0))) return PRTOS_INVALID_PARAM;

        if (!(new_cr3_page = pmm_find_page(val, get_partition(info->sched.current_kthread), 0))) return PRTOS_INVALID_PARAM;

        if (new_cr3_page->type != PPAG_PTDL1) {
            PWARN("Page %x is not page_table_directory_level_1\n", val & PAGE_MASK);
            return PRTOS_INVALID_PARAM;
        }
        phys_page_dec_counter(old_cr3_page);
        phys_page_inc_counter(new_cr3_page);
        info->sched.current_kthread->ctrl.g->karch.ptd_level_1 = val;
        info->sched.current_kthread->ctrl.g->part_ctrl_table->arch.cr3 = val;
        load_cr3(val);
    }

    return PRTOS_OK;
}

__hypercall prtos_s32_t x86_load_cr4_sys(prtos_word_t val) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    if (val & _CR4_PAE) return PRTOS_OP_NOT_ALLOWED;
    info->sched.current_kthread->ctrl.g->part_ctrl_table->arch.cr4 = val;
    val |= _CR4_PSE | _CR4_PGE;
    info->sched.current_kthread->ctrl.g->karch.cr4 = val;
    load_cr4(info->sched.current_kthread->ctrl.g->karch.cr4);

    return PRTOS_OK;
}

__hypercall prtos_s32_t x86_load_tss32_sys(struct x86_tss *__g_param t) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    if (check_gp_param(t, sizeof(struct x86_tss), 4, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    if (!(t->ss1 & 0x3) || ((t->ss1 >> 3) >= (CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES))) return PRTOS_INVALID_PARAM;

    if (t->sp1 >= CONFIG_PRTOS_OFFSET) return PRTOS_INVALID_PARAM;

    if (!(t->ss2 & 0x3) || ((t->ss2 >> 3) >= (CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES))) return PRTOS_INVALID_PARAM;

    if (t->sp2 >= CONFIG_PRTOS_OFFSET) return PRTOS_INVALID_PARAM;

    info->sched.current_kthread->ctrl.g->karch.tss.t.ss1 = t->ss1;
    info->sched.current_kthread->ctrl.g->karch.tss.t.sp1 = t->sp1;
    info->sched.current_kthread->ctrl.g->karch.tss.t.ss2 = t->ss2;
    info->sched.current_kthread->ctrl.g->karch.tss.t.sp2 = t->sp2;

    info->sched.current_kthread->ctrl.g->part_ctrl_table->arch.cr0 |= _CR0_TS;
    info->sched.current_kthread->ctrl.g->karch.cr0 |= _CR0_TS;
    load_cr0(info->sched.current_kthread->ctrl.g->karch.cr0);

    return PRTOS_OK;
}

__hypercall prtos_s32_t x86_load_gdt_sys(struct x86_desc_reg *__g_param desc) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_s32_t gdt_num_of_entries, e;
    struct x86_desc *gdt;
    prtos_u32_t type;

    if (check_gp_param(desc, sizeof(struct x86_desc_reg), 4, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    gdt_num_of_entries = (desc->limit + 1) / sizeof(struct x86_desc);
    if (gdt_num_of_entries > CONFIG_PARTITION_NO_GDT_ENTRIES) return PRTOS_INVALID_PARAM;

    info->sched.current_kthread->ctrl.g->part_ctrl_table->arch.gdtr = *desc;
    memset(info->sched.current_kthread->ctrl.g->karch.gdt_table, 0, CONFIG_PARTITION_NO_GDT_ENTRIES * sizeof(struct x86_desc));
    for (e = 0, gdt = (struct x86_desc *)desc->linear_base; e < gdt_num_of_entries; e++)
        if (is_gdt_desc_valid(&gdt[e], &type) && (type == CODE_DATA_SEG)) {
            info->sched.current_kthread->ctrl.g->karch.gdt_table[e] = gdt[e];
            gdt_table[e] = gdt[e];
        }

    return PRTOS_OK;
}

__hypercall prtos_s32_t x86_load_idtr_sys(struct x86_desc_reg *__g_param desc) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    if (check_gp_param(desc, sizeof(struct x86_desc_reg), 4, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    info->sched.current_kthread->ctrl.g->part_ctrl_table->arch.idtr = *desc;
    info->sched.current_kthread->ctrl.g->part_ctrl_table->arch.max_idt_vec = (desc->limit + 1) / sizeof(struct x86_gate);

    return PRTOS_OK;
}

__hypercall prtos_s32_t x86_update_ss_sp_sys(prtos_word_t ss, prtos_word_t sp, prtos_u32_t level) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    if (!(ss & 0x3) || ((ss >> 3) >= (CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES))) return PRTOS_INVALID_PARAM;

    if (sp >= CONFIG_PRTOS_OFFSET) return PRTOS_INVALID_PARAM;

    switch (level) {
        case 1:
            info->sched.current_kthread->ctrl.g->karch.tss.t.ss1 = ss;
            info->sched.current_kthread->ctrl.g->karch.tss.t.sp1 = sp;
            break;
        case 2:
            info->sched.current_kthread->ctrl.g->karch.tss.t.ss2 = ss;
            info->sched.current_kthread->ctrl.g->karch.tss.t.sp2 = sp;
            break;
        default:
            return PRTOS_INVALID_PARAM;
    }

    return PRTOS_OK;
}

__hypercall prtos_s32_t x86_update_gdt_sys(prtos_s32_t entry, struct x86_desc *__g_param gdt) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    prtos_u32_t type;

    if (check_gp_param(gdt, sizeof(struct x86_desc), 4, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;

    if (entry >= CONFIG_PARTITION_NO_GDT_ENTRIES) return PRTOS_INVALID_PARAM;

    if (is_gdt_desc_valid(gdt, &type) && (type == CODE_DATA_SEG)) {
        info->sched.current_kthread->ctrl.g->karch.gdt_table[entry] = *gdt;
    } else
        return PRTOS_INVALID_PARAM;

    return PRTOS_OK;
}

static inline prtos_s32_t is_gate_desc_valid(struct x86_gate *desc) {
    prtos_word_t base, seg;

    if ((desc->low0 & 0x1fe0) == 0xf00) { /* Only trap gates supported */
        base = (desc->low0 & 0xFFFF0000) | (desc->high0 & 0xFFFF);
        if (base >= CONFIG_PRTOS_OFFSET) {
            kprintf("[gate_desc] Base (0x%x) > PRTOS_OFFSET\n", base);
            return 0;
        }
        seg = desc->high0 >> 16;
        if (!(seg & 0x3)) {
            kprintf("[gate_desc] ring (%d) can't be zero\n", seg & 0x3);
            return 0;
        }
        return 1;
    }

    return 0;
}

__hypercall prtos_s32_t x86_update_idt_sys(prtos_s32_t entry, struct x86_gate *__g_param desc) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();

    if (check_gp_param(desc, sizeof(struct x86_gate), 4, PFLAG_NOT_NULL) < 0) return PRTOS_INVALID_PARAM;
    if (entry < FIRST_USER_IRQ_VECTOR || entry >= IDT_ENTRIES) return PRTOS_INVALID_PARAM;

    if ((desc->low0 & X86GATE_LOW0_P) && !is_gate_desc_valid(desc)) return PRTOS_INVALID_PARAM;

    info->sched.current_kthread->ctrl.g->karch.hyp_idt_table[entry] = *desc;

    return PRTOS_OK;
}

__hypercall void x86_set_if_sys(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    info->sched.current_kthread->ctrl.g->part_ctrl_table->iflags |= _CPU_FLAG_IF;
}

__hypercall void x86_clear_if_sys(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    info->sched.current_kthread->ctrl.g->part_ctrl_table->iflags &= ~_CPU_FLAG_IF;
}

__hypercall void x86_iret_sys(void) {
    local_processor_t *info = GET_LOCAL_PROCESSOR();
    struct x86_irq_stack_frame *stack_frame;

    cpu_ctxt_t *ctxt = info->sched.current_kthread->ctrl.irq_cpu_ctxt;
    stack_frame = (struct x86_irq_stack_frame *)(ctxt->sp);

    if ((stack_frame->cs & 0x3) != (ctxt->cs & 0x3)) {
        ctxt->sp = stack_frame->sp;
        ctxt->ss = stack_frame->ss;
    } else {
        ctxt->sp = ((prtos_address_t)stack_frame) + sizeof(prtos_word_t) * 3;
    }
    ctxt->flags = stack_frame->flags;
    ctxt->ip = stack_frame->ip;
    ctxt->cs = stack_frame->cs;
    info->sched.current_kthread->ctrl.g->part_ctrl_table->iflags = ctxt->flags & _CPU_FLAG_IF;
    ctxt->flags |= _CPU_FLAG_IF;
    ctxt->flags &= ~_CPU_FLAG_IOPL;
}

__hypercall prtos_s32_t override_trap_handle_sys(prtos_s32_t entry, struct trap_handler *__g_param handler) {
    return PRTOS_OK;
}

// Hypercall table
HYPERCALLR_TAB(multi_call_sys, 0);          // 0
HYPERCALLR_TAB(halt_part_sys, 1);           // 1
HYPERCALLR_TAB(suspend_part_sys, 1);        // 2
HYPERCALLR_TAB(resume_part_sys, 1);         // 3
HYPERCALLR_TAB(reset_partition_sys, 1);     // 4
HYPERCALLR_TAB(shutdown_partition_sys, 1);  // 5
HYPERCALLR_TAB(halt_system_sys, 0);         // 6
HYPERCALLR_TAB(reset_system_sys, 1);        // 7
HYPERCALLR_TAB(idle_self_sys, 0);           // 8

HYPERCALLR_TAB(get_time_sys, 2);      // 9
HYPERCALLR_TAB(set_timer_sys, 3);     // 10
HYPERCALLR_TAB(read_object_sys, 4);   // 11
HYPERCALLR_TAB(write_object_sys, 4);  // 12
HYPERCALLR_TAB(seek_object_sys, 3);   // 13
HYPERCALLR_TAB(ctrl_object_sys, 3);   // 14

HYPERCALLR_TAB(clear_irq_mask_sys, 2);  // 15
HYPERCALLR_TAB(set_irq_mask_sys, 2);    // 16
HYPERCALLR_TAB(force_irqs_sys, 2);      // 17
HYPERCALLR_TAB(clear_irqs_sys, 2);      // 18
HYPERCALLR_TAB(route_irq_sys, 3);       // 19

HYPERCALLR_TAB(update_page32_sys, 2);    // 20
HYPERCALLR_TAB(set_page_type_sys, 2);    // 21
HYPERCALLR_TAB(invald_tlb_sys, 1);       // 22
HYPERCALLR_TAB(raise_ipvi_sys, 1);       // 23
HYPERCALLR_TAB(raise_part_ipvi_sys, 2);  // 24

HYPERCALLR_TAB(override_trap_handle_sys, 2);  // 25

HYPERCALLR_TAB(switch_sched_plan_sys, 2);  // 26
HYPERCALLR_TAB(get_gid_by_name_sys, 2);    // 27
HYPERCALLR_TAB(reset_vcpu_sys, 4);         // 28
HYPERCALLR_TAB(halt_vcpu_sys, 1);          // 29
HYPERCALLR_TAB(suspend_vcpu_sys, 1);       // 30
HYPERCALLR_TAB(resume_vcpu_sys, 1);        // 31
HYPERCALLR_TAB(get_vcpuid_sys, 0);         // 32

HYPERCALLR_TAB(x86_load_cr0_sys, 1);      // 33
HYPERCALLR_TAB(x86_load_cr3_sys, 1);      // 34
HYPERCALLR_TAB(x86_load_cr4_sys, 1);      // 35
HYPERCALLR_TAB(x86_load_tss32_sys, 1);    // 36
HYPERCALLR_TAB(x86_load_gdt_sys, 1);      // 37
HYPERCALLR_TAB(x86_load_idtr_sys, 1);     // 38
HYPERCALLR_TAB(x86_update_ss_sp_sys, 3);  // 39
HYPERCALLR_TAB(x86_update_gdt_sys, 2);    // 40
HYPERCALLR_TAB(x86_update_idt_sys, 2);    // 41
HYPERCALL_TAB(x86_set_if_sys, 0);         // 42
HYPERCALL_TAB(x86_clear_if_sys, 0);       // 43
