/*
 * FILE: processor.h
 *
 * AArch64 processor definitions
 *
 * http://www.prtos.org/
 */

#ifndef _ARCH_PRTOS_PROCESSOR_H_
#define _ARCH_PRTOS_PROCESSOR_H_

#ifndef __ASSEMBLY__

/* AArch64 general purpose registers saved on guest trap */
struct cpu_user_regs {
    prtos_u64_t regs[31]; /* x0-x30 */
    prtos_u64_t sp;       /* guest SP_EL1 */
    prtos_u64_t elr;      /* ELR_EL2 (return PC) */
    prtos_u64_t spsr;     /* SPSR_EL2 (saved PSTATE) */
    prtos_u64_t esr;      /* ESR_EL2 (exception syndrome) */
    prtos_u64_t far;      /* FAR_EL2 (fault address) */
    prtos_u64_t hpfar;    /* HPFAR_EL2 (IPA of stage-2 fault) */
};

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
