/*
 * FILE: processor.h
 *
 * Processor functions
 *
 * www.prtos.org
 */

#ifndef _PRTOS_PROCESSOR_H_
#define _PRTOS_PROCESSOR_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#include <irqs.h>
#include <smp.h>
#include <arch/processor.h>

extern void halt_system(void);
extern void reset_system(prtos_u32_t reset_mode);

extern void dump_state(cpu_ctxt_t *regs);
extern void part_panic(cpu_ctxt_t *ctxt, prtos_s8_t *fmt, ...);
extern void system_panic(cpu_ctxt_t *ctxt, prtos_s8_t *fmt, ...);
// extern void stack_backtrace(prtos_u32_t);

extern prtos_u32_t cpu_khz;
extern void setup_cpu(void);
extern void setup_arch_local(prtos_s32_t cpuid);
extern void early_setup_arch_common(void);
extern void setup_arch_common(void);

#endif
