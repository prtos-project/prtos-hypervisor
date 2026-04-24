/*
 * FILE: processor.h
 *
 * LoongArch 64-bit processor definitions
 *
 * http://www.prtos.org/
 */

#ifndef _ARCH_PRTOS_PROCESSOR_H_
#define _ARCH_PRTOS_PROCESSOR_H_

#ifndef __ASSEMBLY__

/* LoongArch general purpose registers saved on guest trap.
 * r0 ($zero) is not saved (always 0). r1 ($ra) through r31 saved. */
struct cpu_user_regs {
    prtos_u64_t ra;    /* r1  */
    prtos_u64_t tp;    /* r2  */
    prtos_u64_t sp;    /* r3  */
    prtos_u64_t a0;    /* r4  */
    prtos_u64_t a1;    /* r5  */
    prtos_u64_t a2;    /* r6  */
    prtos_u64_t a3;    /* r7  */
    prtos_u64_t a4;    /* r8  */
    prtos_u64_t a5;    /* r9  */
    prtos_u64_t a6;    /* r10 */
    prtos_u64_t a7;    /* r11 */
    prtos_u64_t t0;    /* r12 */
    prtos_u64_t t1;    /* r13 */
    prtos_u64_t t2;    /* r14 */
    prtos_u64_t t3;    /* r15 */
    prtos_u64_t t4;    /* r16 */
    prtos_u64_t t5;    /* r17 */
    prtos_u64_t t6;    /* r18 */
    prtos_u64_t t7;    /* r19 */
    prtos_u64_t t8;    /* r20 */
    prtos_u64_t u0;    /* r21 (reserved/percpu) */
    prtos_u64_t fp;    /* r22 */
    prtos_u64_t s0;    /* r23 */
    prtos_u64_t s1;    /* r24 */
    prtos_u64_t s2;    /* r25 */
    prtos_u64_t s3;    /* r26 */
    prtos_u64_t s4;    /* r27 */
    prtos_u64_t s5;    /* r28 */
    prtos_u64_t s6;    /* r29 */
    prtos_u64_t s7;    /* r30 */
    prtos_u64_t s8;    /* r31 */
    prtos_u64_t era;   /* exception return address (CSR.ERA) */
    prtos_u64_t crmd;  /* current mode (CSR.CRMD) */
    prtos_u64_t prmd;  /* pre-exception mode (CSR.PRMD) */
    prtos_u64_t estat; /* exception status (CSR.ESTAT) */
    prtos_u64_t badv;  /* bad virtual address (CSR.BADV) */
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
