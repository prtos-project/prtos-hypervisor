/*
 * FILE: irqs.h
 *
 * IRQS
 *
 * www.prtos.org
 */

#ifndef _PRTOS_IRQS_H_
#define _PRTOS_IRQS_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#ifndef __ASSEMBLY__

#include <linkage.h>
#include <arch/irqs.h>

#define SCHED_PENDING 0x80000000
#define IRQ_IN_PROGRESS (~SCHED_PENDING)

typedef void (*irq_handler_t)(cpu_ctxt_t *, void *);

typedef prtos_s32_t (*trap_handler_t)(cpu_ctxt_t *, prtos_u16_t *);

struct irq_table_entry {
    irq_handler_t handler;
    void *data;
};

#ifndef CONFIG_AARCH64 // For AArch64, CONFIG_NO_HWIRQS is not limited to 32. 
#if (CONFIG_NO_HWIRQS) > 32
#error CONFIG_NO_HWIRQS is greater than 32
#endif
#if (NO_TRAPS) > 32
#error NO_TRAPS is greater than 32
#endif
#endif

extern struct irq_table_entry irq_handler_table[CONFIG_NO_HWIRQS];
extern trap_handler_t trap_handler_table[NO_TRAPS];
extern void setup_irqs(void);
extern irq_handler_t set_irq_handler(prtos_s32_t, irq_handler_t, void *);
extern trap_handler_t set_trap_handler(prtos_s32_t, trap_handler_t);

extern void arch_setup_irqs(void);

// Control over each interrupt
typedef struct {
    void (*enable)(prtos_u32_t irq);
    void (*disable)(prtos_u32_t irq);
    void (*ack)(prtos_u32_t irq);
    void (*end)(prtos_u32_t irq);
    void (*force)(prtos_u32_t irq);
    void (*clear)(prtos_u32_t irq);
} hw_irq_ctrl_t;

extern prtos_u32_t hw_irq_get_mask(prtos_s32_t e);
extern void hw_irq_set_mask(prtos_s32_t e, prtos_u32_t mask);

extern hw_irq_ctrl_t hw_irq_ctrl[CONFIG_NO_HWIRQS];

static inline void hw_disable_irq(prtos_s32_t irq) {
    if ((irq < CONFIG_NO_HWIRQS) && hw_irq_ctrl[irq].disable) hw_irq_ctrl[irq].disable(irq);
}

static inline void hw_enable_irq(prtos_s32_t irq) {
    if ((irq < CONFIG_NO_HWIRQS) && hw_irq_ctrl[irq].enable) hw_irq_ctrl[irq].enable(irq);
}

static inline void hw_ack_irq(prtos_s32_t irq) {
    if ((irq < CONFIG_NO_HWIRQS) && hw_irq_ctrl[irq].ack) hw_irq_ctrl[irq].ack(irq);
}

static inline void hw_end_irq(prtos_s32_t irq) {
    if ((irq < CONFIG_NO_HWIRQS) && hw_irq_ctrl[irq].end) hw_irq_ctrl[irq].end(irq);
}

static inline void hw_force_irq(prtos_s32_t irq) {
    if ((irq < CONFIG_NO_HWIRQS) && hw_irq_ctrl[irq].force) hw_irq_ctrl[irq].force(irq);
}

static inline void hw_clear_irq(prtos_s32_t irq) {
    if ((irq < CONFIG_NO_HWIRQS) && hw_irq_ctrl[irq].clear) hw_irq_ctrl[irq].clear(irq);
}

extern prtos_s32_t mask_hw_irq(prtos_s32_t irq);
extern prtos_s32_t unmask_hw_irq(prtos_s32_t irq);
extern void set_trap_pending(cpu_ctxt_t *ctxt);
extern prtos_s32_t arch_trap_is_sys_ctxt(cpu_ctxt_t *ctxt);
extern prtos_address_t irq_vector_to_address(prtos_s32_t vector);

#ifdef CONFIG_VERBOSE_TRAP
extern prtos_s8_t *trap_to_str[];
#endif

#endif

#endif
