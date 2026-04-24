/*
 * FILE: kthread.h
 *
 * LoongArch 64-bit arch kernel thread
 *
 * http://www.prtos.org/
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
        prtos_u64_t fp;   /* r22 */
        prtos_u64_t s0;   /* r23 */
        prtos_u64_t s1;   /* r24 */
        prtos_u64_t s2;   /* r25 */
        prtos_u64_t s3;   /* r26 */
        prtos_u64_t s4;   /* r27 */
        prtos_u64_t s5;   /* r28 */
        prtos_u64_t s6;   /* r29 */
        prtos_u64_t s7;   /* r30 */
        prtos_u64_t s8;   /* r31 */
        prtos_u64_t sp;
        prtos_u64_t ra;
    } saved_context;

    void *stack;
    struct cpu_info *cpu_info;

    /* LVZ hardware virtualization state */
    prtos_u32_t lvz_enabled;     /* 1 if this guest uses LVZ hw-virt */
    prtos_u32_t guest_gid;       /* Guest ID for TLB tagging */

    /* Guest CSR state (software emulation for hw-virt guests at PLV3) */
    prtos_u64_t gstat;
    prtos_u64_t gcfg;
    prtos_u64_t guest_crmd;      /* 0x0 */
    prtos_u64_t guest_prmd;      /* 0x1 */
    prtos_u64_t guest_euen;      /* 0x2 */
    prtos_u64_t guest_misc;      /* 0x3 */
    prtos_u64_t guest_ecfg;      /* 0x4 */
    prtos_u64_t guest_estat;     /* 0x5 */
    prtos_u64_t guest_era;       /* 0x6 */
    prtos_u64_t guest_badv;      /* 0x7 */
    prtos_u64_t guest_badi;      /* 0x8 */
    prtos_u64_t guest_eentry;    /* 0xC */

    /* TLB CSRs */
    prtos_u64_t guest_tlbidx;    /* 0x10 */
    prtos_u64_t guest_tlbehi;    /* 0x11 */
    prtos_u64_t guest_tlbelo0;   /* 0x12 */
    prtos_u64_t guest_tlbelo1;   /* 0x13 */
    prtos_u64_t guest_asid;      /* 0x18 */
    prtos_u64_t guest_pgdl;      /* 0x19 */
    prtos_u64_t guest_pgdh;      /* 0x1A */
    prtos_u64_t guest_pwcl;      /* 0x1C */
    prtos_u64_t guest_pwch;      /* 0x1D */
    prtos_u64_t guest_stlbps;    /* 0x1E */
    prtos_u64_t guest_rvacfg;    /* 0x1F */

    /* Config CSRs (read-only emulation) */
    prtos_u64_t guest_cpuid;     /* 0x20 */
    prtos_u64_t guest_prcfg1;    /* 0x21 */
    prtos_u64_t guest_prcfg2;    /* 0x22 */
    prtos_u64_t guest_prcfg3;    /* 0x23 */

    /* Scratch registers */
    prtos_u64_t guest_save[16];  /* 0x30-0x3F */

    /* Timer CSRs */
    prtos_u64_t guest_tid;       /* 0x40 */
    prtos_u64_t guest_tcfg;      /* 0x41 */
    prtos_u64_t guest_cntc;      /* 0x43 */
    prtos_u64_t guest_llbctl;    /* 0x60 */

    /* Implementation CSRs */
    prtos_u64_t guest_impctl1;   /* 0x80 */
    prtos_u64_t guest_impctl2;   /* 0x81 */

    /* TLB refill exception CSRs */
    prtos_u64_t guest_tlbrentry; /* 0x88 */
    prtos_u64_t guest_tlbrbadv;  /* 0x89 */
    prtos_u64_t guest_tlbrera;   /* 0x8A */
    prtos_u64_t guest_tlbrsave;  /* 0x8B */
    prtos_u64_t guest_tlbrelo0;  /* 0x8C */
    prtos_u64_t guest_tlbrelo1;  /* 0x8D */
    prtos_u64_t guest_tlbrehi;   /* 0x8E */
    prtos_u64_t guest_tlbrprmd;  /* 0x8F */

    /* Direct Memory Windows */
    prtos_u64_t guest_dmw[4];    /* 0x180-0x183 */

    /* Guest timer emulation (for HW-virt partitions) */
    prtos_u64_t guest_timer_active;
    prtos_u64_t guest_tcfg_deadline; /* Stable counter value when timer expires */
    prtos_u32_t guest_in_tlb_refill; /* Guest is handling a TLB refill exception */

    /* IPI emulation */
    prtos_u64_t ipi_pending;

    /* HWI interrupt injection tracking (bits 7:0 = HWI7..HWI0 injected via GINTC) */
    prtos_u64_t hwi_injected;

    /* Secondary vCPU boot (hw-virt multi-vCPU partitions) */
    prtos_u64_t hsm_entry;      /* Entry point for secondary vCPU */
    prtos_u64_t hsm_opaque;     /* Opaque value passed to secondary vCPU */
};

#endif
