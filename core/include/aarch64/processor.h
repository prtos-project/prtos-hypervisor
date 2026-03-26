/*
 * FILE: processor.h
 *
 * Processor
 *
 * www.prtos.org
 */

#ifndef _ARCH_PRTOS_PROCESSOR_H_
#define _ARCH_PRTOS_PROCESSOR_H_


#ifndef __ASSEMBLY__

#define __DECL_REG(n64, n32) \
    union {                  \
        prtos_u64_t n64;     \
        prtos_u32_t n32;     \
    }

/* On stack VCPU state */
struct cpu_user_regs {
    /*
     * The mapping AArch64 <-> AArch32 is based on D1.20.1 in ARM DDI
     * 0487A.d.
     *
     *         AArch64       AArch32
     */
    __DECL_REG(x0, r0 /*_usr*/);
    __DECL_REG(x1, r1 /*_usr*/);
    __DECL_REG(x2, r2 /*_usr*/);
    __DECL_REG(x3, r3 /*_usr*/);
    __DECL_REG(x4, r4 /*_usr*/);
    __DECL_REG(x5, r5 /*_usr*/);
    __DECL_REG(x6, r6 /*_usr*/);
    __DECL_REG(x7, r7 /*_usr*/);
    __DECL_REG(x8, r8 /*_usr*/);
    __DECL_REG(x9, r9 /*_usr*/);
    __DECL_REG(x10, r10 /*_usr*/);
    __DECL_REG(x11, r11 /*_usr*/);
    __DECL_REG(x12, r12 /*_usr*/);

    __DECL_REG(x13, /* r13_usr */ sp_usr);
    __DECL_REG(x14, /* r14_usr */ lr_usr);

    __DECL_REG(x15, /* r13_hyp */ __unused_sp_hyp);

    __DECL_REG(x16, /* r14_irq */ lr_irq);
    __DECL_REG(x17, /* r13_irq */ sp_irq);

    __DECL_REG(x18, /* r14_svc */ lr_svc);
    __DECL_REG(x19, /* r13_svc */ sp_svc);

    __DECL_REG(x20, /* r14_abt */ lr_abt);
    __DECL_REG(x21, /* r13_abt */ sp_abt);

    __DECL_REG(x22, /* r14_und */ lr_und);
    __DECL_REG(x23, /* r13_und */ sp_und);

    __DECL_REG(x24, r8_fiq);
    __DECL_REG(x25, r9_fiq);
    __DECL_REG(x26, r10_fiq);
    __DECL_REG(x27, r11_fiq);
    __DECL_REG(x28, r12_fiq);
    __DECL_REG(/* x29 */ fp, /* r13_fiq */ sp_fiq);

    __DECL_REG(/* x30 */ lr, /* r14_fiq */ lr_fiq);

    prtos_u64_t sp; /* Valid for hypervisor frames */

    /* Return address and mode */
    __DECL_REG(pc, pc32); /* ELR_EL2 */
    prtos_u64_t cpsr;     /* SPSR_EL2 */
    prtos_u64_t hsr;      /* ESR_EL2 */

    /* The kernel frame should be 16-byte aligned. */
    prtos_u64_t pad0;

    /* Outer guest frame only from here on... */

    union {
        prtos_u64_t spsr_el1; /* AArch64 */
        prtos_u32_t spsr_svc; /* AArch32 */
    };

    /* AArch32 guests only */
    prtos_u32_t spsr_fiq, spsr_irq, spsr_und, spsr_abt;

    /* AArch64 guests only */
    prtos_u64_t sp_el0;
    prtos_u64_t sp_el1, elr_el1;
};

#undef __DECL_REG

#ifdef CONFIG_SMP
#define GET_CPU_ID() __arch_get_local_id()
#define SET_CPU_ID(x) __arch_set_local_id(x)
#define GET_CPU_HWID() __arch_get_local_hw_id()
#define SET_CPU_HWID(x) __arch_set_local_hw_id(x)
#else
#define GET_CPU_ID() 0
#define SET_CPU_ID(x)
#define GET_CPU_HWID() 0
#define SET_CPU_HWID(x)
#endif

/*
#define x86system_panic(...) do { \
        cpu_ctxt_t ctxt; \
        get_cpu_ctxt(&ctxt); \
        system_panic(&ctxt, __VA_ARGS__); \
    } while (0)
*/
#define DCACHE 0
#define ICACHE 0
static inline void set_cache_state(prtos_u32_t cache) {}

#define x86system_panic(...)  \
    do {                      \
        eprintf(__VA_ARGS__); \
        halt_system();        \
    } while (0)

extern void early_delay(prtos_u32_t cycles);
extern prtos_u32_t __arch_get_local_id(void);
extern prtos_u32_t __arch_get_local_hw_id(void);
extern void __arch_set_local_id(prtos_u32_t id);
extern void __arch_set_local_hw_id(prtos_u32_t hw_id);

#endif // __ASSEMBLY__

#endif
