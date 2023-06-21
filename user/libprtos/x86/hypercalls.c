/*
 * FILE: hypercalls.c
 *
 * prtos system calls definitions
 *
 * www.prtos.org
 */

//#include <prtoshypercalls.h>
#include <prtos_inc/hypercalls.h>
#include <hypervisor.h>

prtos_hcall0r(halt_system);
prtos_hcall1r(reset_system, prtos_u32_t, reset_mode);
prtos_hcall1r(halt_partition, prtos_u32_t, partition_id);
prtos_hcall1r(suspend_partition, prtos_u32_t, partition_id);
prtos_hcall1r(resume_partition, prtos_u32_t, partition_id);
prtos_hcall3r(reset_partition, prtos_u32_t, partition_id, prtos_u32_t, reset_mode, prtos_u32_t, status);
prtos_hcall1r(halt_vcpu, prtos_u32_t, vcpu_id);
prtos_hcall1r(suspend_vcpu, prtos_u32_t, vcpu_id);
prtos_hcall1r(resume_vcpu, prtos_u32_t, vcpu_id);
prtos_hcall4r(reset_vcpu, prtos_u32_t, vcpu_id, prtos_address_t, ptd_level_1_table, prtos_address_t, entry, prtos_u32_t, status);

__stdcall prtos_id_t prtos_get_vcpuid(void) {
    prtos_s32_t _r;
    _PRTOS_HCALL0(get_vcpuid_nr, _r);
    return _r;
}
prtos_hcall1r(shutdown_partition, prtos_u32_t, partition_id);
prtos_hcall0r(idle_self);
prtos_hcall4r(read_object, prtos_obj_desc_t, obj_desc, void *, buffer, prtos_u32_t, size, prtos_u32_t *, flags);
prtos_hcall4r(write_object, prtos_obj_desc_t, obj_desc, void *, buffer, prtos_u32_t, size, prtos_u32_t *, flags);
prtos_hcall3r(ctrl_object, prtos_obj_desc_t, obj_desc, prtos_u32_t, cmd, void *, arg);
prtos_hcall3r(seek_object, prtos_obj_desc_t, trace_stream, prtos_u32_t, offset, prtos_u32_t, whence);
prtos_hcall2r(get_time, prtos_u32_t, clock_id, prtos_time_t *, time);

prtos_hcall2r(clear_irqmask, prtos_u32_t, hw_irqs_mask, prtos_u32_t, ext_irqs_pend);
prtos_hcall2r(set_irqmask, prtos_u32_t, hw_irqs_mask, prtos_u32_t, ext_irqs_pend);
prtos_hcall2r(set_irqpend, prtos_u32_t, hw_irq_mask, prtos_u32_t, ext_irq_mask);
prtos_hcall2r(clear_irqpend, prtos_u32_t, hw_irq_mask, prtos_u32_t, ext_irq_mask);
prtos_hcall3r(route_irq, prtos_u32_t, type, prtos_u32_t, irq, prtos_u16_t, vector);
prtos_hcall1r(raise_ipvi, prtos_u8_t, no_ipvi);
prtos_hcall2r(raise_partition_ipvi, prtos_u32_t, partition_id, prtos_u8_t, no_ipvi);

prtos_hcall2r(update_page32, prtos_address_t, p_addr, prtos_u32_t, val);
prtos_hcall2r(set_page_type, prtos_address_t, p_addr, prtos_u32_t, type);

prtos_hcall2r(switch_sched_plan, prtos_u32_t, new_plan_id, prtos_u32_t *, current_plan_id);
prtos_hcall2r(get_gid_by_name, prtos_u8_t *, name, prtos_u32_t, entity);

prtos_hcall1r(x86_load_cr0, prtos_word_t, val);
prtos_hcall1r(x86_load_cr3, prtos_word_t, val);
prtos_hcall1r(x86_load_cr4, prtos_word_t, val);
prtos_hcall1r(x86_load_tss, struct x86_tss *, t);
prtos_hcall1r(x86_load_gdt, struct x86_desc_reg *, desc);
prtos_hcall1r(x86_load_idtr, struct x86_desc_reg *, desc);
prtos_hcall3r(x86_update_ss_sp, prtos_word_t, ss, prtos_word_t, sp, prtos_u32_t, level);
prtos_hcall2r(x86_update_gdt, prtos_s32_t, entry, struct x86_desc *, gdt);
prtos_hcall2r(x86_update_idt, prtos_s32_t, entry, struct x86_gate *, desc);

prtos_hcall0(x86_set_if);
prtos_hcall0(x86_clear_if);

prtos_hcall2r(override_trap_hndl, prtos_s32_t, entry, struct trap_handler *, handler);
prtos_hcall1r(invld_tlb, prtos_address_t, vaddr);

prtos_lazy_hcall1(x86_load_cr0, prtos_word_t, val);
prtos_lazy_hcall1(x86_load_cr3, prtos_word_t, val);
prtos_lazy_hcall1(x86_load_cr4, prtos_word_t, val);
prtos_lazy_hcall1(invld_tlb, prtos_address_t, vaddr);
prtos_lazy_hcall1(x86_load_tss, struct x86_tss *, t);
prtos_lazy_hcall1(x86_load_gdt, struct x86_desc_reg *, desc);
prtos_lazy_hcall1(x86_load_idtr, struct x86_desc_reg *, desc);
prtos_lazy_hcall3(x86_update_ss_sp, prtos_word_t, ss, prtos_word_t, sp, prtos_u32_t, level);
prtos_lazy_hcall2(x86_update_gdt, prtos_s32_t, entry, struct x86_desc *, gdt);
prtos_lazy_hcall2(x86_update_idt, prtos_s32_t, entry, struct x86_gate *, desc);

__stdcall prtos_s32_t prtos_multicall(void *start_addr, void *end_addr) {
    prtos_s32_t _r;
    _PRTOS_HCALL2(start_addr, end_addr, multicall_nr, _r);
    return _r;
}

__stdcall prtos_s32_t prtos_set_timer(prtos_u32_t clock_id, prtos_time_t abs_stime, prtos_time_t interval) {
    prtos_s32_t _r;
    _PRTOS_HCALL5(clock_id, abs_stime, abs_stime >> 32, interval, interval >> 32, set_timer_nr, _r);
    return _r;
}

void prtos_x86_iret(void) {
    __asm__ __volatile__("lcall $(" TO_STR(PRTOS_IRET_CALLGATE_SEL) "), $0x0\n\t");
}
