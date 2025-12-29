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

#define HWIRQS_VECTOR_SIZE ((CONFIG_NO_HWIRQS % 32) ? (CONFIG_NO_HWIRQS / 32) + 1 : (CONFIG_NO_HWIRQS / 32))

#ifdef CONFIG_SMP
#define NR_IPIS 4
#endif

#define UD_HNDL 0x6
#define NM_HNDL 0x7
#define GP_HNDL 0xd
#define PF_HNDL 0xe

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
    prtos_u64_t fp;
    prtos_u64_t sp;
    prtos_u64_t pc;
} cpu_ctxt_t;

#define GET_CTXT_PC(ctxt) ctxt->pc
#define SET_CTXT_PC(ctxt, _pc) \
    do {                       \
        (ctxt)->pc = (_pc);    \
    } while (0)

#define cpu_ctxt_to_hm_cpu_ctxt(cpu_ctxt, hm_cpu_ctxt) \
    do {                                               \
        (hm_cpu_ctxt)->pc = (cpu_ctxt)->pc;            \
        (hm_cpu_ctxt)->psr = (cpu_ctxt)->sp;        \
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
    // (*iflags) &= ~_CPU_FLAG_IF;
}

static inline prtos_s32_t are_part_ctrl_table_irqs_set(prtos_u32_t iflags) {
    // return (iflags & _CPU_FLAG_IF) ? 1 : 0;
    return 1;
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
    //return (ctxt->cs & 0x3) ? 0 : 1;
    return 1;
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

/* Access to system registers */
#define __stringify_1(x...) #x
#define __stringify(x...)   __stringify_1(x)

#define WRITE_SYSREG64(v, name) do {                    \
    prtos_u64_t _r = (v);                                  \
    asm volatile("msr "__stringify(name)", %0" : : "r" (_r));       \
} while (0)
#define READ_SYSREG64(name) ({                          \
    prtos_u64_t _r;                                        \
    asm volatile("mrs  %0, "__stringify(name) : "=r" (_r));         \
    _r; })

#define READ_SYSREG(name)     READ_SYSREG64(name)
#define WRITE_SYSREG(v, name) WRITE_SYSREG64(v, name)


#define SAVE_REG(reg, field)  field = READ_SYSREG64(reg)

static inline void get_cpu_ctxt(cpu_ctxt_t *ctxt) {
    // SAVE_REG(x19, ctxt->x19);
    // SAVE_REG(x20, ctxt->x20);
    // SAVE_REG(x21, ctxt->x21);
    // SAVE_REG(x22, ctxt->x22);
    // SAVE_REG(x23, ctxt->x23);
    // SAVE_REG(x24, ctxt->x24);
    // SAVE_REG(x25, ctxt->x25);
    // SAVE_REG(x26, ctxt->x26);
    // SAVE_REG(x27, ctxt->x27);
    // SAVE_REG(fp, ctxt->fp);
    // SAVE_REG(sp, ctxt->sp);
    // SAVE_REG(pc, ctxt->pc);
    // SAVE_REG(sp, ctxt->sp);
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
