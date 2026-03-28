/*
 * FILE: processor.h
 *
 * RISC-V 64-bit processor definitions
 *
 * www.prtos.org
 */

#ifndef _ARCH_PRTOS_PROCESSOR_H_
#define _ARCH_PRTOS_PROCESSOR_H_

#ifndef __ASSEMBLY__

/* RISC-V general purpose registers saved on guest trap */
struct cpu_user_regs {
    prtos_u64_t ra;    /* x1  */
    prtos_u64_t sp;    /* x2  */
    prtos_u64_t gp;    /* x3  */
    prtos_u64_t tp;    /* x4  */
    prtos_u64_t t0;    /* x5  */
    prtos_u64_t t1;    /* x6  */
    prtos_u64_t t2;    /* x7  */
    prtos_u64_t s0;    /* x8  / fp */
    prtos_u64_t s1;    /* x9  */
    prtos_u64_t a0;    /* x10 */
    prtos_u64_t a1;    /* x11 */
    prtos_u64_t a2;    /* x12 */
    prtos_u64_t a3;    /* x13 */
    prtos_u64_t a4;    /* x14 */
    prtos_u64_t a5;    /* x15 */
    prtos_u64_t a6;    /* x16 */
    prtos_u64_t a7;    /* x17 */
    prtos_u64_t s2;    /* x18 */
    prtos_u64_t s3;    /* x19 */
    prtos_u64_t s4;    /* x20 */
    prtos_u64_t s5;    /* x21 */
    prtos_u64_t s6;    /* x22 */
    prtos_u64_t s7;    /* x23 */
    prtos_u64_t s8;    /* x24 */
    prtos_u64_t s9;    /* x25 */
    prtos_u64_t s10;   /* x26 */
    prtos_u64_t s11;   /* x27 */
    prtos_u64_t t3;    /* x28 */
    prtos_u64_t t4;    /* x29 */
    prtos_u64_t t5;    /* x30 */
    prtos_u64_t t6;    /* x31 */
    prtos_u64_t sepc;  /* exception PC */
    prtos_u64_t sstatus; /* status register */
    prtos_u64_t scause;  /* trap cause */
    prtos_u64_t stval;   /* trap value */
    prtos_u64_t hstatus; /* hypervisor status (SPV etc.) */
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
