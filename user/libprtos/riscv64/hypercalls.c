/*
 * FILE: hypercalls.c
 *
 * prtos system calls definitions for RISC-V 64
 *
 * www.prtos.org
 */

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

__stdcall prtos_s32_t prtos_multicall(void *start_addr, void *end_addr) {
    prtos_s32_t _r;
    _PRTOS_HCALL2(start_addr, end_addr, multicall_nr, _r);
    return _r;
}

__stdcall prtos_s32_t prtos_set_timer(prtos_u32_t clock_id, prtos_time_t abs_stime, prtos_time_t interval) {
    prtos_s32_t _r;
    _PRTOS_HCALL3(clock_id, abs_stime, interval, set_timer_nr, _r);
    return _r;
}

void prtos_x86_iret(void) {
    register prtos_u64_t _a0 __asm__("a0") = 44;  /* PRTOS_IRET_NR = NR_HYPERCALLS */
    __asm__ __volatile__("ecall" : "+r"(_a0) : : "memory");
}

__stdcall prtos_s32_t prtos_raise_partition_trap(prtos_u32_t trap_nr) {
    register prtos_u64_t _a0 __asm__("a0") = 45;  /* PRTOS_RAISE_TRAP_NR = NR_HYPERCALLS + 1 */
    register prtos_u64_t _a1 __asm__("a1") = (prtos_u64_t)trap_nr;
    __asm__ __volatile__("ecall" : "+r"(_a0) : "r"(_a1) : "a2", "a3", "a4", "a5", "memory");
    return (prtos_s32_t)_a0;
}
