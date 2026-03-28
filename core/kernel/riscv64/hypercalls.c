/*
 * FILE: hypercalls.c
 *
 * RISC-V 64-bit hypercall table
 *
 * www.prtos.org
 */

#include <hypercalls.h>

/*
 * RISC-V 64-bit hypercall table macros.
 * Use .quad (8-byte) for function pointers.
 */
#define HYPERCALLR_TAB_RV64(_hc, _args)              \
    __asm__(".section .hypercallstab, \"a\"\n\t"     \
            ".quad " #_hc "\n\t"                     \
            ".previous\n\t"                          \
            ".section .hypercallflagstab, \"a\"\n\t" \
            ".long (0x80000000|" #_args ")\n\t"      \
            ".previous\n\t")

#define HYPERCALL_TAB_RV64(_hc, _args)               \
    __asm__(".section .hypercallstab, \"a\"\n\t"     \
            ".quad " #_hc "\n\t"                     \
            ".previous\n\t"                          \
            ".section .hypercallflagstab, \"a\"\n\t" \
            ".long (" #_args ")\n\t"                 \
            ".previous\n\t")

#define NULL_HYPERCALL_TAB_RV64()                    \
    __asm__(".section .hypercallstab, \"a\"\n\t"     \
            ".quad 0\n\t"                            \
            ".previous\n\t"                          \
            ".section .hypercallflagstab, \"a\"\n\t" \
            ".long 0\n\t"                            \
            ".previous\n\t")

/* Hypercall table entries - must be in numeric order (0 .. NR_HYPERCALLS-1) */

HYPERCALLR_TAB_RV64(multi_call_sys, 0);         /* 0  multicall */
HYPERCALLR_TAB_RV64(halt_part_sys, 1);          /* 1  halt_partition */
HYPERCALLR_TAB_RV64(suspend_part_sys, 1);       /* 2  suspend_partition */
HYPERCALLR_TAB_RV64(resume_part_sys, 1);        /* 3  resume_partition */
HYPERCALLR_TAB_RV64(reset_partition_sys, 3);    /* 4  reset_partition */
HYPERCALLR_TAB_RV64(shutdown_partition_sys, 1); /* 5  shutdown_partition */
HYPERCALLR_TAB_RV64(halt_system_sys, 0);        /* 6  halt_system */
HYPERCALLR_TAB_RV64(reset_system_sys, 1);       /* 7  reset_system */
HYPERCALLR_TAB_RV64(idle_self_sys, 0);          /* 8  idle_self */

HYPERCALLR_TAB_RV64(get_time_sys, 2);     /* 9  get_time */
HYPERCALLR_TAB_RV64(set_timer_sys, 3);    /* 10 set_timer */
HYPERCALLR_TAB_RV64(read_object_sys, 4);  /* 11 read_object */
HYPERCALLR_TAB_RV64(write_object_sys, 4); /* 12 write_object */
HYPERCALLR_TAB_RV64(seek_object_sys, 3);  /* 13 seek_object */
HYPERCALLR_TAB_RV64(ctrl_object_sys, 3);  /* 14 ctrl_object */

HYPERCALLR_TAB_RV64(clear_irq_mask_sys, 2); /* 15 clear_irqmask */
HYPERCALLR_TAB_RV64(set_irq_mask_sys, 2);   /* 16 set_irqmask */
HYPERCALLR_TAB_RV64(force_irqs_sys, 2);     /* 17 set_irqpend */
HYPERCALLR_TAB_RV64(clear_irqs_sys, 2);     /* 18 clear_irqpend */
HYPERCALLR_TAB_RV64(route_irq_sys, 3);      /* 19 route_irq */

NULL_HYPERCALL_TAB_RV64();                   /* 20 update_page32 (x86) */
NULL_HYPERCALL_TAB_RV64();                   /* 21 set_page_type (x86) */
NULL_HYPERCALL_TAB_RV64();                   /* 22 invld_tlb (x86) */
HYPERCALLR_TAB_RV64(raise_ipvi_sys, 1);      /* 23 raise_ipvi */
HYPERCALLR_TAB_RV64(raise_part_ipvi_sys, 2); /* 24 raise_partition_ipvi */
NULL_HYPERCALL_TAB_RV64();                   /* 25 override_trap_hndl (x86) */

HYPERCALLR_TAB_RV64(switch_sched_plan_sys, 2); /* 26 switch_sched_plan */
HYPERCALLR_TAB_RV64(get_gid_by_name_sys, 2);   /* 27 get_gid_by_name */
HYPERCALLR_TAB_RV64(reset_vcpu_sys, 4);        /* 28 reset_vcpu */
HYPERCALLR_TAB_RV64(halt_vcpu_sys, 1);         /* 29 halt_vcpu */
HYPERCALLR_TAB_RV64(suspend_vcpu_sys, 1);      /* 30 suspend_vcpu */
HYPERCALLR_TAB_RV64(resume_vcpu_sys, 1);       /* 31 resume_vcpu */
HYPERCALLR_TAB_RV64(get_vcpuid_sys, 0);        /* 32 get_vcpuid */

/* 33-43: x86-specific hypercalls - not available on RISC-V */
NULL_HYPERCALL_TAB_RV64(); /* 33 */
NULL_HYPERCALL_TAB_RV64(); /* 34 */
NULL_HYPERCALL_TAB_RV64(); /* 35 */
NULL_HYPERCALL_TAB_RV64(); /* 36 */
NULL_HYPERCALL_TAB_RV64(); /* 37 */
NULL_HYPERCALL_TAB_RV64(); /* 38 */
NULL_HYPERCALL_TAB_RV64(); /* 39 */
NULL_HYPERCALL_TAB_RV64(); /* 40 */
NULL_HYPERCALL_TAB_RV64(); /* 41 */
NULL_HYPERCALL_TAB_RV64(); /* 42 */
NULL_HYPERCALL_TAB_RV64(); /* 43 */
