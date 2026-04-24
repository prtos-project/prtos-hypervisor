/*
 * FILE: panic.c
 *
 * LoongArch 64-bit panic/dump state
 *
 * http://www.prtos.org/
 */

#include <kthread.h>
#include <stdc.h>
#include <processor.h>

void dump_state(cpu_ctxt_t *regs) {
    kprintf("PC: 0x%llx SP: 0x%llx\n", (unsigned long long)regs->pc,
            (unsigned long long)regs->sp);
}
