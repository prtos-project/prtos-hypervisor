/*
 * FILE: processor.h
 *
 * Processor
 *
 * www.prtos.org
 */

#ifndef _ARCH_PRTOS_PROCESSOR_H_
#define _ARCH_PRTOS_PROCESSOR_H_

#define _DETECTED_I586 0x1
#define _PAE_SUPPORT 0x2
#define _PSE_SUPPORT 0x4
#define _PGE_SUPPORT 0x8
#define _X2APIC_SUPPORT 0x20
#define _LM_SUPPORT 0x10

// CR0 bits
#define _CR0_PE (1 << 0)
#define _CR0_EM (1 << 2)
#define _CR0_TS (1 << 3)
#define _CR0_ET (1 << 4)
#define _CR0_WP (1 << 16)
#define _CR0_AM (1 << 18)
#define _CR0_NW (1 << 29)
#define _CR0_CD (1 << 30)
#define _CR0_PG (1 << 31)

// CR4 bits
#define _CR4_VME (1 << 0)
#define _CR4_PSE (1 << 4)
#define _CR4_PAE (1 << 5)
#define _CR4_PGE (1 << 7)

// DR7 bits
#define _DR7_Lx(n) (1 << ((n)*2))
#define _DR7_Gx(n) (1 << (((n)*2) + 1))
#define _DR7_RW(n, x) (((x)&0x3) << (((n)*4) + 16))
#define _DR7_LEN(n, x) (((x)&0x3) << (((n)*4) + 18))
#define _DR7_Mask(n) (0xF << (((n)*4) + 16))

// DR6 bits
#define _DR6_Bx(n) (1 << (n))
#define _DR6_BD 1 << 13
#define _DR6_BS 1 << 14
#define _DR6_BT 1 << 15

// MSRs
#define MSR_IA32_EFER 0xc0000080
#define _MSR_EFER_LME (1 << 3)
#define MSR_IA32_FS_BASE 0xc0000100
#define MSR_IA32_GS_BASE 0xc0000101
#define MSR_IA32_APIC_BASE 0x1b
#define _MSR_APICBASE_BSP (1 << 8)
#define _MSR_APICBASE_EXTD (1 << 10)
#define _MSR_APICBASE_ENABLE (1 << 11)
#define _MSR_APICBASE_BASE (0xfffff << 12)

// _CPUID features
#define _CPUID_FPU (1 << 0)
#define _CPUID_PSE (1 << 3)
#define _CPUID_MSR (1 << 5)
#define _CPUID_PAE (1 << 6)
#define _CPUID_CX8 (1 << 8)
#define _CPUID_PGE (1 << 13)
#define _CPUID_FXSR (1 << 24)
#define _CPUID_CMOV (1 << 15)
#define _CPUID_PRTOSM (1 << 25)
#define _CPUID_PRTOSM2 (1 << 26)
#define _CPUID_X2APIC (1 << 21)

#define _CPUID_LM (1 << 29)
#define _CPUID_3DNOW (1 << 31)

// EFLAGS' flags
#define _CPU_FLAG_TF 0x00000100
#define _CPU_FLAG_IF 0x00000200
#define _CPU_FLAG_IOPL 0x00003000
#define _CPU_FLAG_NT 0x00004000
#define _CPU_FLAG_VM 0x00020000
#define _CPU_FLAG_AC 0x00040000
#define _CPU_FLAG_VIF 0x00080000
#define _CPU_FLAG_VIP 0x00100000
#define _CPU_FLAG_ID 0x00200000

#define CPU_FLAGS_IF_BIT 9
#define CPU_FLAGS_IOPL_BIT 12

#ifndef __ASSEMBLY__

struct x86_desc_reg {
    prtos_u16_t limit;
    prtos_address_t linear_base;
} __PACKED;

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

#endif

#endif
