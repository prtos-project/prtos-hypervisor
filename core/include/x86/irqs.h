/*
 * FILE: irqs.h
 *
 * IRQS
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_IRQS_H_
#define _PRTOS_ARCH_IRQS_H_

/* existing traps (just 32 in the ia32 arch) */
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

/* existing hw interrupts (required by PIC and APIC) */
#define FIRST_EXTERNAL_VECTOR 0x20
#define FIRST_USER_IRQ_VECTOR 0x30

#ifdef CONFIG_SMP
#define NR_IPIS 4
#endif

#define UD_HNDL 0x6
#define NM_HNDL 0x7
#define GP_HNDL 0xd
#define PF_HNDL 0xe

#ifndef __ASSEMBLY__

struct x86_irq_stack_frame {
    prtos_word_t ip;
    prtos_word_t cs;
    prtos_word_t flags;
    prtos_word_t sp;
    prtos_word_t ss;
};

typedef struct _cpu_ctxt {
    prtos_word_t bx;
    prtos_word_t cx;
    prtos_word_t dx;
    prtos_word_t si;
    prtos_word_t di;
    prtos_word_t bp;
    prtos_word_t ax;
    prtos_word_t ds;
    prtos_word_t es;
    prtos_word_t fs;
    prtos_word_t gs;
    struct _cpu_ctxt *prev;
    prtos_word_t irq_nr;
    prtos_word_t err_code;

    prtos_word_t ip;
    prtos_word_t cs;
    prtos_word_t flags;
    prtos_word_t sp;
    prtos_word_t ss;
} cpu_ctxt_t;

#define GET_ECODE(ctxt) ctxt->err_code
#define GET_CTXT_PC(ctxt) ctxt->ip
#define SET_CTXT_PC(ctxt, _pc) \
    do {                       \
        (ctxt)->ip = (_pc);    \
    } while (0)

#define cpu_ctxt_to_hm_cpu_ctxt(cpu_ctxt, hm_cpu_ctxt) \
    do {                                               \
        (hm_cpu_ctxt)->pc = (cpu_ctxt)->ip;            \
        (hm_cpu_ctxt)->psr = (cpu_ctxt)->flags;        \
    } while (0)

#define print_hm_cpu_ctxt(ctxt) kprintf("ip: 0x%lx flags: 0x%lx\n", (ctxt)->pc, (ctxt)->psr)

#ifdef CONFIG_SMP

typedef struct {
    // saved registers
    prtos_u32_t bx;
    prtos_u32_t cx;
    prtos_u32_t dx;
    prtos_u32_t si;
    prtos_u32_t di;
    prtos_u32_t bp;
    prtos_u32_t ax;
    prtos_u32_t ds;
    prtos_u32_t es;
    prtos_u32_t fs;
    prtos_u32_t gs;
    // ipi
    prtos_u32_t nr_ipi;
    // processor state frame
    prtos_u32_t ip;
    prtos_u32_t cs;
    prtos_u32_t eflags;
    prtos_u32_t sp;
    prtos_u32_t ss;
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
    //(*iflags)&=~_CPU_FLAG_IF;
    // x86 does not require it
}

static inline prtos_s32_t are_part_ctrl_table_traps_set(prtos_u32_t iflags) {
    return 1;
}

static inline void mask_part_ctrl_table_irq(prtos_u32_t *mask, prtos_u32_t bitmap) {
    // doing nothing
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
    SAVE_REG(ebx, ctxt->bx);
    SAVE_REG(ecx, ctxt->cx);
    SAVE_REG(edx, ctxt->dx);
    SAVE_REG(esi, ctxt->si);
    SAVE_REG(edi, ctxt->di);
    SAVE_REG(ebp, ctxt->bp);
    SAVE_REG(eax, ctxt->ax);
    SAVE_REG(ds, ctxt->ds);
    SAVE_REG(es, ctxt->es);
    SAVE_REG(fs, ctxt->fs);
    SAVE_REG(gs, ctxt->gs);
    SAVE_REG(cs, ctxt->cs);
    SAVE_REG(esp, ctxt->sp);
    SAVE_REG(ss, ctxt->ss);
    save_eip(ctxt->ip);
    hw_save_flags(ctxt->flags);
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
