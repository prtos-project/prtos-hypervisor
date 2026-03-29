/*
 * FILE: kthread.h
 *
 * RISC-V 64-bit arch kernel thread
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_KTHREAD_H_
#define _PRTOS_ARCH_KTHREAD_H_

#include <arch/processor.h>

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

struct cpu_info {
    struct cpu_user_regs guest_cpu_user_regs;
    unsigned long elr;
    prtos_u32_t flags;
};

struct kthread_arch {
    struct {
        prtos_u64_t s0;
        prtos_u64_t s1;
        prtos_u64_t s2;
        prtos_u64_t s3;
        prtos_u64_t s4;
        prtos_u64_t s5;
        prtos_u64_t s6;
        prtos_u64_t s7;
        prtos_u64_t s8;
        prtos_u64_t s9;
        prtos_u64_t s10;
        prtos_u64_t s11;
        prtos_u64_t sp;
        prtos_u64_t ra;
    } saved_context;

    void *stack;
    struct cpu_info *cpu_info;

    /* VS-mode guest state */
    prtos_u64_t vsstatus;
    prtos_u64_t vsie;
    prtos_u64_t vstvec;
    prtos_u64_t vsscratch;
    prtos_u64_t vsepc;
    prtos_u64_t vscause;
    prtos_u64_t vstval;
    prtos_u64_t vsip;
    prtos_u64_t vsatp;

    /* HS-mode virtualization control */
    prtos_u64_t hstatus;
    prtos_u64_t hgatp;      /* Stage-2 page table base (G-stage) */

    /* Stage-2 page table pointers */
    prtos_u64_t *s2_root;   /* Root page table, page-aligned */
    prtos_u64_t *s2_l1[4];  /* L1 tables */
    prtos_u64_t *s2_l2[8];  /* L2 tables for 4KB page mappings */
    prtos_s32_t s2_l2_count;

    /* Guest SBI timer emulation (for HW-virt partitions) */
    prtos_u64_t guest_timer_active;  /* Guest has pending SBI timer */

    /* SBI IPI emulation: pending virtual supervisor software interrupt */
    prtos_u64_t vssip_pending;

    /* SBI HSM emulation (for HW-virt multi-vCPU partitions) */
    prtos_u64_t hsm_entry;    /* Entry point set by SBI HART_START */
    prtos_u64_t hsm_opaque;   /* Opaque value passed as a1 to started hart */
};

#endif
