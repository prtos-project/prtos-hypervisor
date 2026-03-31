/*
 * FILE: irqs.h
 *
 * IRQS for amd64
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_IRQS_H_
#define _PRTOS_ARCH_IRQS_H_

#define NO_TRAPS 32

#include __PRTOS_INCFLD(arch/prtos_def.h)
#include __PRTOS_INCFLD(guest.h)

#ifndef __ASSEMBLY__
struct trap_handler {
    prtos_u16_t cs;
    prtos_u64_t ip;
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

#define UD_HNDL 0x6
#define NM_HNDL 0x7
#define GP_HNDL 0xd
#define PF_HNDL 0xe

/* Hypercall vector for software interrupt based hypercalls */
#define HYPERCALL_VECTOR 0x82

#ifndef __ASSEMBLY__

struct x86_irq_stack_frame {
    prtos_u64_t ip;
    prtos_u64_t cs;
    prtos_u64_t flags;
    prtos_u64_t sp;
    prtos_u64_t ss;
};

typedef struct _cpu_ctxt {
    prtos_u64_t r15;
    prtos_u64_t r14;
    prtos_u64_t r13;
    prtos_u64_t r12;
    prtos_u64_t r11;
    prtos_u64_t r10;
    prtos_u64_t r9;
    prtos_u64_t r8;
    prtos_u64_t bx;
    prtos_u64_t cx;
    prtos_u64_t dx;
    prtos_u64_t si;
    prtos_u64_t di;
    prtos_u64_t bp;
    prtos_u64_t ax;
    struct _cpu_ctxt *prev;
    prtos_u64_t irq_nr;
    prtos_u64_t err_code;

    prtos_u64_t ip;
    prtos_u64_t cs;
    prtos_u64_t flags;
    prtos_u64_t sp;
    prtos_u64_t ss;
} cpu_ctxt_t;

#define GET_ECODE(ctxt) ((ctxt)->err_code)
#define GET_CTXT_PC(ctxt) ((ctxt)->ip)
#define SET_CTXT_PC(ctxt, _pc) \
    do {                       \
        (ctxt)->ip = (_pc);    \
    } while (0)

#define cpu_ctxt_to_hm_cpu_ctxt(cpu_ctxt, hm_cpu_ctxt) \
    do {                                               \
        (hm_cpu_ctxt)->pc = (cpu_ctxt)->ip;            \
        (hm_cpu_ctxt)->psr = (cpu_ctxt)->flags;        \
    } while (0)

#define print_hm_cpu_ctxt(ctxt) kprintf("ip: 0x%llx flags: 0x%llx\n", (ctxt)->pc, (ctxt)->psr)

#ifdef CONFIG_SMP

typedef struct {
    prtos_u64_t r15;
    prtos_u64_t r14;
    prtos_u64_t r13;
    prtos_u64_t r12;
    prtos_u64_t r11;
    prtos_u64_t r10;
    prtos_u64_t r9;
    prtos_u64_t r8;
    prtos_u64_t bx;
    prtos_u64_t cx;
    prtos_u64_t dx;
    prtos_u64_t si;
    prtos_u64_t di;
    prtos_u64_t bp;
    prtos_u64_t ax;
    prtos_u64_t nr_ipi;
    prtos_u64_t ip;
    prtos_u64_t cs;
    prtos_u64_t rflags;
    prtos_u64_t sp;
    prtos_u64_t ss;
} ipi_ctxt_t;

typedef void (*ipi_irq_t)(ipi_ctxt_t *ctxt, void *data);

extern ipi_irq_t ipi_table[NR_IPIS];
#endif

static inline void init_part_ctrl_table_irqs(prtos_u32_t *iflags) {
    (*iflags) &= ~_CPU_FLAG_IF;
}

static inline prtos_s32_t are_part_ctrl_table_irqs_set(prtos_u32_t iflags) {
    return (iflags & _CPU_FLAG_IF) ? 1 : 0;
}

static inline void disable_part_ctrl_table_irqs(prtos_u32_t *iflags) {
}

static inline prtos_s32_t are_part_ctrl_table_traps_set(prtos_u32_t iflags) {
    return 1;
}

static inline void mask_part_ctrl_table_irq(prtos_u32_t *mask, prtos_u32_t bitmap) {
}

static inline prtos_s32_t is_hpv_irq_ctxt(cpu_ctxt_t *ctxt) {
    return (ctxt->cs & 0x3) ? 0 : 1;
}

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

#define SAVE_REG(reg, field) __asm__ __volatile__("mov %%" #reg ", %0\n\t" : "=m"(field) : : "memory");

static inline void get_cpu_ctxt(cpu_ctxt_t *ctxt) {
    SAVE_REG(rbx, ctxt->bx);
    SAVE_REG(rcx, ctxt->cx);
    SAVE_REG(rdx, ctxt->dx);
    SAVE_REG(rsi, ctxt->si);
    SAVE_REG(rdi, ctxt->di);
    SAVE_REG(rbp, ctxt->bp);
    SAVE_REG(rax, ctxt->ax);
    SAVE_REG(r8, ctxt->r8);
    SAVE_REG(r9, ctxt->r9);
    SAVE_REG(r10, ctxt->r10);
    SAVE_REG(r11, ctxt->r11);
    SAVE_REG(r12, ctxt->r12);
    SAVE_REG(r13, ctxt->r13);
    SAVE_REG(r14, ctxt->r14);
    SAVE_REG(r15, ctxt->r15);
    ctxt->cs = 0;
    save_rip(ctxt->ip);
    hw_save_flags(ctxt->flags);
    ctxt->sp = save_stack();
}

extern prtos_u32_t x86_hw_irqs_mask[CONFIG_NO_CPUS];

#endif  //__ASSEMBLY__
#endif  //_PRTOS_KERNEL_

/* x86-exception-list */
#define DIVIDE_ERROR 0
#define DEBUG_EXCEPTION 1
#define NMI_INTERRUPT 2
#define BREAKPOINT_EXCEPTION 3
#define OVERFLOW_EXCEPTION 4
#define BOUNDS_EXCEPTION 5
#define INVALID_OPCODE 6
#define DEVICE_NOT_AVAILABLE 7
#define DOUBLE_FAULT 8
#define COPROCESSOR_OVERRRUN 9
#define INVALID_TSS 10
#define SEGMENT_NOT_PRESENT 11
#define STACK_SEGMENT_FAULT 12
#define GENERAL_PROTECTION_FAULT 13
#define PAGE_FAULT 14
#define RESERVED2_EXCEPTION 15
#define FLOATING_POINT_ERROR 16
#define ALIGNMENT_CHECK 17
#define MACHINE_CHECK 18
#define SIMD_EXCEPTION 19

#endif
