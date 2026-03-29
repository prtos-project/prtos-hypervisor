/*
 * FILE: arch.c
 *
 * RISC-V 64 BAIL architecture initialization
 *
 * www.prtos.org
 */
#include <prtos.h>
#include <irqs.h>

extern void _bail_trap_dispatch(void);

void init_arch(void) {
    /* Register trap dispatch stub address in PCT */
    prtos_get_pct()->arch.trap_entry = (prtos_u64_t)_bail_trap_dispatch;
}

void part_halt(void) {
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}

/*
 * _bail_trap_dispatch_c - C part of virtual IRQ dispatch.
 *
 * Called from _bail_trap_dispatch assembly stub with saved regs on stack.
 * Reads irq_vector from PCT, fills trap_ctxt_t.irq_nr, calls the handler.
 */
void _bail_trap_dispatch_c(trap_ctxt_t *ctxt) {
    partition_control_table_t *pct = prtos_get_pct();
    prtos_u32_t vector = pct->arch.irq_vector;

    ctxt->irq_nr = (prtos_u64_t)vector;

    if (vector < TRAPTAB_LENGTH && part_trap_handlers_table[vector]) {
        part_trap_handlers_table[vector](ctxt);
    }
}
