/*
 * FILE: hypercalls.h
 *
 * Processor-related hypercalls definition
 *
 * www.prtos.org
 *
 */

#ifndef _PRTOS_ARCH_HYPERCALLS_H_
#define _PRTOS_ARCH_HYPERCALLS_H_

// hypercall-numbers
#define __MULTICALL_NR 0
#define __HALT_PARTITION_NR 1
#define __SUSPEND_PARTITION_NR 2
#define __RESUME_PARTITION_NR 3
#define __RESET_PARTITION_NR 4
#define __SHUTDOWN_PARTITION_NR 5
#define __HALT_SYSTEM_NR 6
#define __RESET_SYSTEM_NR 7
#define __IDLE_SELF_NR 8

#define __GET_TIME_NR 9
#define __SET_TIMER_NR 10
#define __READ_OBJECT_NR 11
#define __WRITE_OBJECT_NR 12
#define __SEEK_OBJECT_NR 13
#define __CTRL_OBJECT_NR 14

#define __CLEAR_IRQ_MASK_NR 15
#define __SET_IRQ_MASK_NR 16
#define __FORCE_IRQS_NR 17
#define __CLEAR_IRQS_NR 18
#define __ROUTE_IRQ_NR 19

#define __UPDATE_PAGE32_NR 20
#define __SET_PAGE_TYPE_NR 21
#define __INVLD_TLB_NR 22
#define __RAISE_IPVI_NR 23
#define __RAISE_PARTITION_IPVI_NR 24
#define __OVERRIDE_TRAP_HNDL_NR 25

#define __SWITCH_SCHED_PLAN_NR 26
#define __GET_GID_BY_NAME_NR 27
#define __RESET_VCPU_NR 28
#define __HALT_VCPU_NR 29
#define __SUSPEND_VCPU_NR 30
#define __RESUME_VCPU_NR 31

#define __GET_VCPUID_NR 32

#define x86_load_cr0_nr 33
#define x86_load_cr3_nr 34
#define x86_load_cr4_nr 35
#define x86_load_tss_nr 36
#define x86_load_gdt_nr 37
#define x86_load_idtr_nr 38
#define x86_update_ss_sp_nr 39
#define x86_update_gdt_nr 40
#define x86_update_idt_nr 41
#define x86_set_if_nr 42
#define x86_clear_if_nr 43
#define NR_HYPERCALLS 44

#endif
