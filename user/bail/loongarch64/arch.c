/*
 * FILE: arch.c
 *
 * LoongArch 64 BAIL architecture initialization
 *
 * http://www.prtos.org/
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
 */
void _bail_trap_dispatch_c(trap_ctxt_t *ctxt) {
    partition_control_table_t *pct = prtos_get_pct();
    /* irq_vector has bit 31 set as a sentinel (vector | 0x80000000) so
     * that vector 0 is distinguishable from "no delivery".  Mask it off. */
    prtos_u32_t vector = pct->arch.irq_vector & 0x7FFFFFFF;

    ctxt->irq_nr = (prtos_u64_t)vector;

    if (vector < TRAPTAB_LENGTH && part_trap_handlers_table[vector]) {
        part_trap_handlers_table[vector](ctxt);
    }
}
