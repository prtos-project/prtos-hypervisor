/*
 * FILE: hypercalls.h
 *
 * Hypercalls definition
 *
 * www.prtos.org
 */

#ifndef _PRTOS_HYPERCALLS_H_
#define _PRTOS_HYPERCALLS_H_

#include __PRTOS_INCFLD(arch/hypercalls.h)

/* abi abi versions */
#define PRTOS_ABI_VERSION 1
#define PRTOS_ABI_SUBVERSION 0
#define PRTOS_ABI_REVISION 0

#define PRTOS_API_VERSION 1
#define PRTOS_API_SUBVERSION 0
#define PRTOS_API_REVISION 0

// Generic hypercalls
#define HYPERCALL_NOT_IMPLEMENTED (~0)
#define multicall_nr __MULTICALL_NR
#define halt_partition_nr __HALT_PARTITION_NR
#define suspend_partition_nr __SUSPEND_PARTITION_NR
#define resume_partition_nr __RESUME_PARTITION_NR
#define reset_partition_nr __RESET_PARTITION_NR
#define PRTOS_RESET_MODE 0x1

/* reset values */
#define PRTOS_COLD_RESET 0x0
#define PRTOS_WARM_RESET 0x1

#define shutdown_partition_nr __SHUTDOWN_PARTITION_NR
#define halt_system_nr __HALT_SYSTEM_NR
#define reset_system_nr __RESET_SYSTEM_NR

#define reset_vcpu_nr __RESET_VCPU_NR
#define halt_vcpu_nr __HALT_VCPU_NR
#define suspend_vcpu_nr __SUSPEND_VCPU_NR
#define resume_vcpu_nr __RESUME_VCPU_NR

#define get_vcpuid_nr __GET_VCPUID_NR

#define idle_self_nr __IDLE_SELF_NR
#define get_time_nr __GET_TIME_NR

#define PRTOS_HW_CLOCK (0x0)
#define PRTOS_EXEC_CLOCK (0x1)
#define PRTOS_WATCHDOG_TIMER (0x2)

#define set_timer_nr __SET_TIMER_NR
#define read_object_nr __READ_OBJECT_NR
#define write_object_nr __WRITE_OBJECT_NR
#define seek_object_nr __SEEK_OBJECT_NR
#define PRTOS_OBJ_SEEK_CUR 0x0
#define PRTOS_OBJ_SEEK_SET 0x1
#define PRTOS_OBJ_SEEK_END 0x2
#define ctrl_object_nr __CTRL_OBJECT_NR

#define clear_irqmask_nr __CLEAR_IRQ_MASK_NR
#define set_irqmask_nr __SET_IRQ_MASK_NR
#define set_irqpend_nr __FORCE_IRQS_NR
#define clear_irqpend_nr __CLEAR_IRQS_NR
#define route_irq_nr __ROUTE_IRQ_NR
#define PRTOS_TRAP_TYPE 0x0
#define PRTOS_HWIRQ_TYPE 0x1
#define PRTOS_EXTIRQ_TYPE 0x2

#define update_page32_nr __UPDATE_PAGE32_NR
#define set_page_type_nr __SET_PAGE_TYPE_NR
#define PPAG_STD 0
#define PPAG_PTDL1 1
#define PPAG_PTDL2 2
#define PPAG_PTDL3 3
// ...

#define invld_tlb_nr __INVLD_TLB_NR
#define override_trap_hndl_nr __OVERRIDE_TRAP_HNDL_NR
#define raise_ipvi_nr __RAISE_IPVI_NR
#define raise_partition_ipvi_nr __RAISE_PARTITION_IPVI_NR

#define flush_cache_nr __FLUSH_CACHE_NR
#define set_cache_state_nr __SET_CACHE_STATE_NR
#define PRTOS_DCACHE 0x1
#define PRTOS_ICACHE 0x2

#define switch_sched_plan_nr __SWITCH_SCHED_PLAN_NR
#define get_gid_by_name_nr __GET_GID_BY_NAME_NR
#define PRTOS_PARTITION_NAME 0x0
#define PRTOS_PLAN_NAME 0x1

// Returning values
#define PRTOS_OK (0)
#define PRTOS_NO_ACTION (-1)
#define PRTOS_UNKNOWN_HYPERCALL (-2)
#define PRTOS_INVALID_PARAM (-3)
#define PRTOS_PERM_ERROR (-4)
#define PRTOS_INVALID_CONFIG (-5)
#define PRTOS_INVALID_MODE (-6)
#define PRTOS_OP_NOT_ALLOWED (-7)
#define PRTOS_MULTICALL_ERROR (-8)

#ifndef __ASSEMBLY__

#define HYPERCALLR_TAB(_hc, _args)                   \
    __asm__(".section .hypercallstab, \"a\"\n\t"     \
            ".align 4\n\t"                           \
            ".long " #_hc "\n\t"                     \
            ".previous\n\t"                          \
            ".section .hypercallflagstab, \"a\"\n\t" \
            ".long (0x80000000|" #_args ")\n\t"      \
            ".previous\n\t")

#define HYPERCALL_TAB(_hc, _args)                    \
    __asm__(".section .hypercallstab, \"a\"\n\t"     \
            ".align 4\n\t"                           \
            ".long " #_hc "\n\t"                     \
            ".previous\n\t"                          \
            ".section .hypercallflagstab, \"a\"\n\t" \
            ".long (" #_args ")\n\t"                 \
            ".previous\n\t")

#endif

#endif
