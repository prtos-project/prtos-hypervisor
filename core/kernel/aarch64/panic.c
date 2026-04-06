/*
 * FILE: panic.c
 *
 * AArch64 panic handling
 *
 * http://www.prtos.org/
 */

#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>

void dump_state(cpu_ctxt_t *ctxt) {
    kprintf("CPU state:\n");
    kprintf("  PC: 0x%lx  SP: 0x%lx\n", ctxt->pc, ctxt->sp);
}
