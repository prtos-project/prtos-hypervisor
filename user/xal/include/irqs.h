/*
 * FILE: irqs.h
 *
 * www.prtos.org
 */

#ifndef _XAL_IRQS_H_
#define _XAL_IRQS_H_

#include <arch/irqs.h>
#include <prtos.h>

#define XAL_PRTOSEXT_TRAP(_prtos_ext_irq_number) (((_prtos_ext_irq_number)-PRTOS_VT_EXT_FIRST) + 224)

typedef void (*trap_handler_t)(trap_ctxt_t *);

extern trap_handler_t part_trap_handlers_table[];
extern prtos_s32_t install_trap_handler(prtos_s32_t trap_number, trap_handler_t handler);
extern void setup_irqs(void);

#endif
