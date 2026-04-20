/*
 * FILE: irqs.h
 *
 * LoongArch 64-bit IRQ definitions
 *
 * http://www.prtos.org/
 */

#ifndef _PRTOS_ARCH_IRQS_H_
#define _PRTOS_ARCH_IRQS_H_

/* existing traps */
#define NO_TRAPS 32

#include __PRTOS_INCFLD(arch/prtos_def.h)
#include __PRTOS_INCFLD(guest.h)

#ifndef __ASSEMBLY__
struct trap_handler {
    prtos_u16_t cs;
    prtos_address_t ip;
};
#endif

#ifdef _PRTOS_KERNEL_
#define IDT_ENTRIES 256

#define FIRST_EXTERNAL_VECTOR 0x20
#define FIRST_USER_IRQ_VECTOR 0x30

#define HWIRQS_VECTOR_SIZE ((CONFIG_NO_HWIRQS % 32) ? (CONFIG_NO_HWIRQS / 32) + 1 : (CONFIG_NO_HWIRQS / 32))

#ifdef CONFIG_SMP
#define NR_IPIS 4
#endif

/* LoongArch exception codes (ESTAT.Ecode field, bits 21:16) */
#define LOONGARCH_ECODE_INT   0x0   /* Interrupt */
#define LOONGARCH_ECODE_PIL   0x1   /* Page invalid - load */
#define LOONGARCH_ECODE_PIS   0x2   /* Page invalid - store */
#define LOONGARCH_ECODE_PIF   0x3   /* Page invalid - fetch */
#define LOONGARCH_ECODE_PME   0x4   /* Page modification */
#define LOONGARCH_ECODE_PNR   0x5   /* Page not readable */
#define LOONGARCH_ECODE_PNX   0x6   /* Page not executable */
#define LOONGARCH_ECODE_PPI   0x7   /* Page privilege illegal */
#define LOONGARCH_ECODE_ADEF  0x8   /* Address error - fetch */
#define LOONGARCH_ECODE_ADEM  0x8   /* Address error - memory (subcode 1) */
#define LOONGARCH_ECODE_ALE   0x9   /* Alignment error */
#define LOONGARCH_ECODE_BCE   0xA   /* Boundary check */
#define LOONGARCH_ECODE_SYS   0xB   /* Syscall */
#define LOONGARCH_ECODE_BRK   0xC   /* Breakpoint */
#define LOONGARCH_ECODE_INE   0xD   /* Instruction not exist */
#define LOONGARCH_ECODE_IPE   0xE   /* Instruction privilege error */
#define LOONGARCH_ECODE_FPD   0xF   /* FP disabled */
#define LOONGARCH_ECODE_FPE   0x12  /* FP exception */
#define LOONGARCH_ECODE_GSPR  0x1A  /* Guest sensitive privileged resource */
#define LOONGARCH_ECODE_HVC   0x1B  /* Hypervisor call */

/* LoongArch LVZ (Virtualization) CSR numbers */
#define CSR_GTLBC    0x15  /* Guest TLB Control */
#define CSR_TRGP     0x16  /* TLB Read Guest Port */
#define CSR_GSTAT    0x50  /* Guest Status */
#define CSR_GCFG     0x51  /* Guest Config */
#define CSR_GINTC    0x52  /* Guest Interrupt Control */
#define CSR_GCNTC    0x53  /* Guest Counter Compensation */

/* GSTAT fields */
#define CSR_GSTAT_VM       (1UL << 0)   /* Currently in VM mode */
#define CSR_GSTAT_PVM      (1UL << 1)   /* Previous mode was VM */
#define CSR_GSTAT_GID_SHIFT  16
#define CSR_GSTAT_GID_MASK   0xFF

/* GCFG fields */
#define CSR_GCFG_MATC_ROOT   (1UL << 4)  /* Root-managed address translation */
#define CSR_GCFG_TIT         (1UL << 9)  /* Trap on Timer */
#define CSR_GCFG_TOE         (1UL << 11) /* Trap on Exception */
#define CSR_GCFG_TOP         (1UL << 13) /* Trap on Privilege */
#define CSR_GCFG_TORU        (1UL << 15) /* Trap on Root Unimplement */
#define CSR_GCFG_GCI_SECURE  (2UL << 20) /* Guest Cache Instruction: trap */
#define CSR_GCFG_MATP_ROOT   (1UL << 1)  /* MATP: root supported */

/* GTLBC fields */
#define CSR_GTLBC_USETGID   (1UL << 12) /* Use TGID for TLB tagging */
#define CSR_GTLBC_TOTI      (1UL << 13) /* Trap on TLB Invalid */
#define CSR_GTLBC_TGID_SHIFT  16
#define CSR_GTLBC_TGID_MASK   0xFF

/* GINTC fields */
#define CSR_GINTC_VIP_SHIFT  0   /* Virtual Interrupt Pending bits */
#define CSR_GINTC_VIP_MASK   0xFF
#define CSR_GINTC_PIP_SHIFT  8   /* Physical Interrupt Pending bits */
#define CSR_GINTC_HC_SHIFT   16  /* Hardware Counter bits */

/* CPUCFG register 2 LVZ fields */
#define CPUCFG2_LVZP         (1UL << 10)  /* LVZ Present */

/* CSR_SAVE5: used as LVZ active flag */
#define CSR_SAVE5    0x35

/* LoongArch interrupt bit positions in ESTAT.IS / ECFG.LIE */
#define LOONGARCH_INT_SWI0   0   /* Software interrupt 0 */
#define LOONGARCH_INT_SWI1   1   /* Software interrupt 1 */
#define LOONGARCH_INT_HWI0   2   /* Hardware interrupt 0 */
#define LOONGARCH_INT_HWI1   3   /* Hardware interrupt 1 */
#define LOONGARCH_INT_HWI2   4
#define LOONGARCH_INT_HWI3   5
#define LOONGARCH_INT_HWI4   6
#define LOONGARCH_INT_HWI5   7
#define LOONGARCH_INT_HWI6   8
#define LOONGARCH_INT_HWI7   9
#define LOONGARCH_INT_PMC    10  /* Performance counter */
#define LOONGARCH_INT_TI     11  /* Timer interrupt */
#define LOONGARCH_INT_IPI    12  /* Inter-Processor Interrupt */

#ifndef __ASSEMBLY__

typedef struct _cpu_ctxt {
    prtos_u64_t fp;    /* callee-saved frame pointer */
    prtos_u64_t s0;
    prtos_u64_t s1;
    prtos_u64_t s2;
    prtos_u64_t s3;
    prtos_u64_t s4;
    prtos_u64_t s5;
    prtos_u64_t s6;
    prtos_u64_t s7;
    prtos_u64_t s8;
    prtos_u64_t sp;
    prtos_u64_t pc;  /* ERA */
    prtos_u64_t irq_nr;
} cpu_ctxt_t;

#define GET_ECODE(ctxt) 0
#define GET_CTXT_PC(ctxt) (ctxt)->pc
#define SET_CTXT_PC(ctxt, _pc) \
    do {                       \
        (ctxt)->pc = (_pc);    \
    } while (0)

#define cpu_ctxt_to_hm_cpu_ctxt(cpu_ctxt, hm_cpu_ctxt) \
    do {                                               \
        (hm_cpu_ctxt)->pc = (cpu_ctxt)->pc;            \
        (hm_cpu_ctxt)->psr = (cpu_ctxt)->sp;           \
    } while (0)

#define print_hm_cpu_ctxt(ctxt) kprintf("pc: 0x%lx sp: 0x%lx\n", (ctxt)->pc, (ctxt)->psr)

static inline void init_part_ctrl_table_irqs(prtos_u32_t *iflags) {
}

static inline prtos_s32_t are_part_ctrl_table_irqs_set(prtos_u32_t iflags) {
    return 1;
}

static inline void disable_part_ctrl_table_irqs(prtos_u32_t *iflags) {
}

static inline prtos_s32_t are_part_ctrl_table_traps_set(prtos_u32_t iflags) {
    return 1;
}

static inline void mask_part_ctrl_table_irq(prtos_u32_t *mask, prtos_u32_t bitmap) {
}

prtos_s32_t is_hpv_irq_ctxt(cpu_ctxt_t *ctxt);

extern void fix_stack(cpu_ctxt_t *ctxt, partition_control_table_t *part_ctrl_table, prtos_s32_t irq_nr, prtos_s32_t vector, prtos_s32_t trap);

static inline prtos_s32_t arch_emul_trap_irq(cpu_ctxt_t *ctxt, partition_control_table_t *part_ctrl_table, prtos_s32_t irq_nr) {
    fix_stack(ctxt, part_ctrl_table, irq_nr, part_ctrl_table->trap_to_vector[irq_nr], 1);
    return part_ctrl_table->trap_to_vector[irq_nr];
}

static inline prtos_s32_t arch_emul_hw_irq(cpu_ctxt_t *ctxt, partition_control_table_t *part_ctrl_table, prtos_s32_t irq_nr) {
    fix_stack(ctxt, part_ctrl_table, irq_nr, part_ctrl_table->hw_irq_to_vector[irq_nr], 0);
    return part_ctrl_table->hw_irq_to_vector[irq_nr];
}

static inline prtos_s32_t arch_emul_ext_irq(cpu_ctxt_t *ctxt, partition_control_table_t *part_ctrl_table, prtos_s32_t irq_nr) {
    fix_stack(ctxt, part_ctrl_table, irq_nr, part_ctrl_table->ext_irq_to_vector[irq_nr], 0);
    return part_ctrl_table->ext_irq_to_vector[irq_nr];
}

/* LoongArch CSR read/write macros */
#define LOONGARCH_CSR_CRMD    0x0
#define LOONGARCH_CSR_PRMD    0x1
#define LOONGARCH_CSR_EUEN    0x2
#define LOONGARCH_CSR_ECFG    0x4
#define LOONGARCH_CSR_ESTAT   0x5
#define LOONGARCH_CSR_ERA     0x6
#define LOONGARCH_CSR_BADV    0x7
#define LOONGARCH_CSR_EENTRY  0xC
#define LOONGARCH_CSR_TLBIDX  0x10
#define LOONGARCH_CSR_TLBEHI  0x11
#define LOONGARCH_CSR_TLBELO0 0x12
#define LOONGARCH_CSR_TLBELO1 0x13
#define LOONGARCH_CSR_ASID    0x18
#define LOONGARCH_CSR_PGDL    0x19
#define LOONGARCH_CSR_PGDH    0x1A
#define LOONGARCH_CSR_PGD     0x1B
#define LOONGARCH_CSR_PWCL    0x1C
#define LOONGARCH_CSR_PWCH    0x1D
#define LOONGARCH_CSR_STLBPS  0x1E
#define LOONGARCH_CSR_CPUID   0x20
#define LOONGARCH_CSR_SAVE0   0x30
#define LOONGARCH_CSR_SAVE1   0x31
#define LOONGARCH_CSR_SAVE2   0x32
#define LOONGARCH_CSR_SAVE3   0x33
#define LOONGARCH_CSR_TID     0x40
#define LOONGARCH_CSR_TCFG    0x41
#define LOONGARCH_CSR_TVAL    0x42
#define LOONGARCH_CSR_CNTC    0x43
#define LOONGARCH_CSR_TICLR   0x44
#define LOONGARCH_CSR_TLBRENTRY 0x88
#define LOONGARCH_CSR_DMW0    0x180
#define LOONGARCH_CSR_DMW1    0x181
#define LOONGARCH_CSR_DMW2    0x182
#define LOONGARCH_CSR_DMW3    0x183

/* LVZ (Virtualization) CSRs */
#define LOONGARCH_CSR_GSTAT   0x50
#define LOONGARCH_CSR_GCFG    0x51
#define LOONGARCH_CSR_GINTC   0x52
#define LOONGARCH_CSR_GCNTC   0x53

/* CRMD field definitions */
#define LOONGARCH_CRMD_PLV_MASK  0x3
#define LOONGARCH_CRMD_IE        (1 << 2)
#define LOONGARCH_CRMD_DA        (1 << 3)
#define LOONGARCH_CRMD_PG        (1 << 4)
#define LOONGARCH_CRMD_DATF_MASK (0x3 << 5)
#define LOONGARCH_CRMD_DATM_MASK (0x3 << 7)

/* ESTAT Ecode extraction */
#define LOONGARCH_ESTAT_ECODE_SHIFT 16
#define LOONGARCH_ESTAT_ECODE_MASK  0x3F
#define LOONGARCH_ESTAT_IS_MASK     0x1FFF  /* Interrupt status bits 12:0 */

static inline void get_cpu_ctxt(cpu_ctxt_t *ctxt) {
}

extern prtos_u32_t x86_hw_irqs_mask[CONFIG_NO_CPUS];

#endif  //__ASSEMBLY__
#endif  //_PRTOS_KERNEL_

/* LoongArch trap numbers for partition trap dispatch */
#define LOONGARCH64_ILLEGAL_INSTR          0
#define LOONGARCH64_INSTR_ACCESS_FAULT     1
#define LOONGARCH64_LOAD_ACCESS_FAULT      2
#define LOONGARCH64_STORE_ACCESS_FAULT     3
#define LOONGARCH64_INSTR_PAGE_FAULT       4
#define LOONGARCH64_LOAD_PAGE_FAULT        5
#define LOONGARCH64_STORE_PAGE_FAULT       6
#define LOONGARCH64_INSTR_MISALIGNED       7
#define LOONGARCH64_LOAD_MISALIGNED        8

#endif
