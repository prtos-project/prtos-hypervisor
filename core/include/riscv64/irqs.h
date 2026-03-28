/*
 * FILE: irqs.h
 *
 * RISC-V 64-bit IRQ definitions
 *
 * www.prtos.org
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

/* RISC-V trap numbers (scause exception codes) */
#define RISCV_INSTR_MISALIGNED    0
#define RISCV_INSTR_ACCESS_FAULT  1
#define RISCV_ILLEGAL_INSTR       2
#define RISCV_BREAKPOINT          3
#define RISCV_LOAD_MISALIGNED     4
#define RISCV_LOAD_ACCESS_FAULT   5
#define RISCV_STORE_MISALIGNED    6
#define RISCV_STORE_ACCESS_FAULT  7
#define RISCV_ECALL_FROM_U        8
#define RISCV_ECALL_FROM_S        9
#define RISCV_ECALL_FROM_VS       10
#define RISCV_ECALL_FROM_M        11
#define RISCV_INSTR_PAGE_FAULT    12
#define RISCV_LOAD_PAGE_FAULT     13
#define RISCV_STORE_PAGE_FAULT    15
#define RISCV_INSTR_GUEST_PAGE_FAULT 20
#define RISCV_LOAD_GUEST_PAGE_FAULT  21
#define RISCV_VIRTUAL_INSTR          22
#define RISCV_STORE_GUEST_PAGE_FAULT 23

#ifndef __ASSEMBLY__

typedef struct _cpu_ctxt {
    prtos_u64_t s0;   /* callee-saved frame pointer */
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
    prtos_u64_t pc;  /* sepc */
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

/* CSR read/write macros */
#define READ_CSR(csr)                                     \
    ({                                                    \
        prtos_u64_t _val;                                 \
        asm volatile("csrr %0, " #csr : "=r"(_val));     \
        _val;                                             \
    })

#define WRITE_CSR(csr, val)                               \
    do {                                                  \
        asm volatile("csrw " #csr ", %0" : : "r"(val));  \
    } while (0)

#define SET_CSR(csr, val)                                 \
    do {                                                  \
        asm volatile("csrs " #csr ", %0" : : "r"(val));  \
    } while (0)

#define CLEAR_CSR(csr, val)                                \
    do {                                                   \
        asm volatile("csrc " #csr ", %0" : : "r"(val));   \
    } while (0)

static inline void get_cpu_ctxt(cpu_ctxt_t *ctxt) {
}

extern prtos_u32_t x86_hw_irqs_mask[CONFIG_NO_CPUS];

#endif  //__ASSEMBLY__
#endif  //_PRTOS_KERNEL_

/* RISC-V trap numbers for partition trap dispatch */
#define RISCV64_ILLEGAL_INSTR          0
#define RISCV64_INSTR_ACCESS_FAULT     1
#define RISCV64_LOAD_ACCESS_FAULT      2
#define RISCV64_STORE_ACCESS_FAULT     3
#define RISCV64_INSTR_PAGE_FAULT       4
#define RISCV64_LOAD_PAGE_FAULT        5
#define RISCV64_STORE_PAGE_FAULT       6
#define RISCV64_INSTR_MISALIGNED       7
#define RISCV64_LOAD_MISALIGNED        8

#endif
