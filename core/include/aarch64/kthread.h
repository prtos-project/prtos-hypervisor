/*
 * FILE: kthread.h
 *
 * Arch kernel thread
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_KTHREAD_H_
#define _PRTOS_ARCH_KTHREAD_H_

//#include <irqs.h>
#include <arch/processor.h>
//#include <arch/segments.h>
//#include <arch/prtos_def.h>
//#include <arch/atomic.h>

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

/* ARM64 VFP instruction requires fpregs address to be 128-byte aligned */
#define __vfp_aligned __attribute__((aligned(16)))

struct vfp_state {
    /*
     * When SVE is enabled for the guest, fpregs memory will be used to
     * save/restore P0-P15 registers, otherwise it will be used for the V0-V31
     * registers.
     */
    prtos_u64_t fpregs[64] __vfp_aligned;

#ifdef CONFIG_ARM64_SVE
    /*
     * When SVE is enabled for the guest, sve_zreg_ctx_end points to memory
     * where Z0-Z31 registers and FFR can be saved/restored, it points at the
     * end of the Z0-Z31 space and at the beginning of the FFR space, it's done
     * like that to ease the save/restore assembly operations.
     */
    prtos_u64_t *sve_zreg_ctx_end;
#endif

    prtos_u64_t fpcr;
    prtos_u64_t fpexc32_el2;
    prtos_u64_t fpsr;
};


struct cpu_info {
    struct cpu_user_regs guest_cpu_user_regs;
    unsigned long elr;
    prtos_u32_t flags;
};

struct kthread_arch {
    struct {
        prtos_u64_t x19;
        prtos_u64_t x20;
        prtos_u64_t x21;
        prtos_u64_t x22;
        prtos_u64_t x23;
        prtos_u64_t x24;
        prtos_u64_t x25;
        prtos_u64_t x26;
        prtos_u64_t x27;
        prtos_u64_t x28;
        prtos_u64_t fp;
        prtos_u64_t sp;
        prtos_u64_t pc;
    } saved_context;

    void *stack;

    /*
     * Points into ->stack, more convenient than doing pointer arith
     * all the time.
     */
    struct cpu_info *cpu_info;

    /* Fault Status */
    prtos_u64_t far;
    prtos_u32_t esr;

    prtos_u32_t ifsr; /* 32-bit guests only */
    prtos_u32_t afsr0, afsr1;

    /* MMU */
    prtos_u64_t vbar;
    prtos_u64_t ttbcr;
    prtos_u64_t ttbr0, ttbr1;

    prtos_u32_t dacr; /* 32-bit guests only */
    prtos_u64_t par;
    prtos_u64_t mair;
    prtos_u64_t amair;

    /* Control Registers */
    prtos_u64_t sctlr;
    prtos_u64_t actlr;
    prtos_u32_t cpacr;

    prtos_u32_t contextidr;
    prtos_u64_t tpidr_el0;
    prtos_u64_t tpidr_el1;
    prtos_u64_t tpidrro_el0;

    /* HYP configuration */
#ifdef CONFIG_ARM64_SVE
    prtos_u64_t zcr_el1;
    prtos_u64_t zcr_el2;
#endif

    prtos_u64_t cptr_el2;
    prtos_u64_t hcr_el2;
    prtos_u64_t mdcr_el2;

    prtos_u32_t teecr, teehbr; /* ThumbEE, 32-bit guests only */

    /* Float-pointer */
    struct vfp_state vfp;

    /* CP 15 */
    prtos_u32_t csselr;
    prtos_u64_t vmpidr;

    /* Holds gic context data */
    // union gic_state_data gic;  // will be used in the future
    prtos_u64_t lr_mask;

    // struct vgic_cpu vgic;

    /* Timer registers  */
    prtos_u64_t cntkctl;

    // struct vtimer phys_timer;
    // struct vtimer virt_timer;
    // bool vtimer_initialized;
};


#endif
