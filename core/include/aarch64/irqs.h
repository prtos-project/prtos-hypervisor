/*
 * FILE: irqs.h
 *
 * AArch64 IRQ definitions
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

/* AArch64 exception class values (ESR_EL2.EC) */
#define AARCH64_EC_SVC64     0x15  /* SVC from AArch64 */
#define AARCH64_EC_HVC64     0x16  /* HVC from AArch64 */
#define AARCH64_EC_SMC64     0x17  /* SMC from AArch64 */
#define AARCH64_EC_SYS64     0x18  /* MSR/MRS trap */
#define AARCH64_EC_IABT_LOW  0x20  /* Instruction abort from lower EL */
#define AARCH64_EC_IABT_CUR  0x21  /* Instruction abort from current EL */
#define AARCH64_EC_DABT_LOW  0x24  /* Data abort from lower EL */
#define AARCH64_EC_DABT_CUR  0x25  /* Data abort from current EL */

#ifndef __ASSEMBLY__

typedef struct _cpu_ctxt {
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
    prtos_u64_t x29;
    prtos_u64_t x30;
    prtos_u64_t sp;
    prtos_u64_t pc;  /* ELR_EL2 */
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

static inline void get_cpu_ctxt(cpu_ctxt_t *ctxt) {
}

extern prtos_u32_t x86_hw_irqs_mask[CONFIG_NO_CPUS];

#endif  //__ASSEMBLY__
#endif  //_PRTOS_KERNEL_

/* AArch64 trap numbers for partition trap dispatch */
#define AARCH64_UNDEFINED_INSTR     0
#define AARCH64_SVC_TRAP            1
#define AARCH64_PREFETCH_ABORT      2
#define AARCH64_DATA_ABORT          3
#define AARCH64_IRQ_TRAP            4
#define AARCH64_FIQ_TRAP            5
#define AARCH64_SERROR              6
#define AARCH64_INSTR_PAGE_FAULT    7
#define AARCH64_LOAD_PAGE_FAULT     8

#endif
