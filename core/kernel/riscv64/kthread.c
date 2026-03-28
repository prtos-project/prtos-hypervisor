/*
 * FILE: kthread.c
 *
 * RISC-V 64-bit kernel thread arch-dependent code
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
#include <arch/prtos_def.h>

void switch_kthread_arch_pre(kthread_t *new, kthread_t *current) {
}

void switch_kthread_arch_post(kthread_t *current) {
    /* Set G-stage page table for the new partition */
    if (current->ctrl.g && current->ctrl.g->karch.hgatp) {
        __asm__ __volatile__(
            "csrw hgatp, %0\n\t"
            ".word 0x62000073\n\t"   /* hfence.gvma */
            : : "r"(current->ctrl.g->karch.hgatp) : "memory"
        );
    } else {
        /* Idle kthread: disable G-stage translation (bare mode) */
        __asm__ __volatile__(
            "csrw hgatp, zero\n\t"
            ".word 0x62000073\n\t"   /* hfence.gvma */
            : : : "memory"
        );
    }
}

extern void kthread_startup_wrapper(void);

void setup_kstack(kthread_t *k, void *start_up, prtos_address_t entry_point) {
    prtos_u64_t *sp = (prtos_u64_t *)(&k->kstack[CONFIG_KSTACK_SIZE]);
    /* Build a stack frame matching the CONTEXT_SWITCH restore sequence:
     * ld s0-s11, ra from stack.
     * s0 = start_up, s1 = entry_point, ra = kthread_startup_wrapper
     */
    *(--sp) = 0ULL;  /* padding for 16-byte alignment */
    *(--sp) = (prtos_u64_t)kthread_startup_wrapper; /* ra (offset 96) */
    *(--sp) = 0ULL;                                 /* s11 */
    *(--sp) = 0ULL;                                 /* s10 */
    *(--sp) = 0ULL;                                 /* s9 */
    *(--sp) = 0ULL;                                 /* s8 */
    *(--sp) = 0ULL;                                 /* s7 */
    *(--sp) = 0ULL;                                 /* s6 */
    *(--sp) = 0ULL;                                 /* s5 */
    *(--sp) = 0ULL;                                 /* s4 */
    *(--sp) = 0ULL;                                 /* s3 */
    *(--sp) = 0ULL;                                 /* s2 */
    *(--sp) = (prtos_u64_t)entry_point;             /* s1 */
    *(--sp) = (prtos_u64_t)start_up;                /* s0 */
    k->ctrl.kstack = (prtos_address_t *)sp;
}

void kthread_arch_init(kthread_t *k) {
}

void setup_kthread_arch(kthread_t *k) {
}

void setup_pct_arch(partition_control_table_t *part_ctrl_table, kthread_t *k) {
    /* RISC-V: minimal PCT arch setup */
}
