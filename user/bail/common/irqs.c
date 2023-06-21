/*
 * FILE: irqs.c
 *
 * Generic traps' handler
 *
 * www.prtos.org
 */

#include <prtos.h>
#include <stdio.h>
#include <irqs.h>

trap_handler_t part_trap_handlers_table[TRAPTAB_LENGTH];

static void unexpected_trap(trap_ctxt_t *ctxt) {
#ifdef CONFIG_x86
    printf("[P%d:%d] Unexpected trap 0x%x (ip: 0x%x)\n", PRTOS_PARTITION_SELF, prtos_get_vcpuid(), ctxt->irq_nr, ctxt->ip);
#endif
}

prtos_s32_t install_trap_handler(prtos_s32_t trap_number, trap_handler_t handler) {
    if (trap_number < 0 || trap_number > TRAPTAB_LENGTH) return -1;

    if (handler)
        part_trap_handlers_table[trap_number] = handler;
    else
        part_trap_handlers_table[trap_number] = unexpected_trap;
    return 0;
}

void setup_irqs(void) {
    prtos_s32_t e;

    if (prtos_get_vcpuid() == 0)
        for (e = 0; e < TRAPTAB_LENGTH; e++) part_trap_handlers_table[e] = unexpected_trap;

    for (e = 0; e < PRTOS_VT_EXT_MAX; e++) prtos_route_irq(PRTOS_EXTIRQ_TYPE, e, 224 + e);
}
