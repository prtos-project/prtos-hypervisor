/*
 * FILE: panic.c
 *
 * Code executed in a panic situation for amd64
 *
 * www.prtos.org
 */

#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>

#ifdef CONFIG_DEBUG
static void stack_backtrace(prtos_word_t bp) {
}
#endif

void dump_state(cpu_ctxt_t *ctxt) {
    prtos_word_t bp, cr2;
    prtos_u16_t cs;

    kprintf("CPU state:\n");
    cr2 = save_cr2();
    kprintf("RIP: 0x%x:[<0x%llx>]", 0xffff & ctxt->cs, ctxt->ip);
    if (ctxt->cs & 0x3)
        kprintf(" RSP: 0x%x:[<0x%llx>]", 0xffff & ctxt->ss, ctxt->sp);
    else {
        prtos_word_t csp = save_stack();
        prtos_u16_t css;
        save_ss(css);
        kprintf(" RSP: 0x%x:[<0x%llx>]", 0xffff & css, csp);
    }
    kprintf(" RFLAGS: 0x%llx  \n", ctxt->flags);
    kprintf("RAX: 0x%llx RBX: 0x%llx RCX: 0x%llx RDX: 0x%llx\n", ctxt->ax, ctxt->bx, ctxt->cx, ctxt->dx);
    kprintf("RSI: 0x%llx RDI: 0x%llx RBP: 0x%llx\n", ctxt->si, ctxt->di, ctxt->bp);
    kprintf("R8:  0x%llx R9:  0x%llx R10: 0x%llx R11: 0x%llx\n", ctxt->r8, ctxt->r9, ctxt->r10, ctxt->r11);
    kprintf("R12: 0x%llx R13: 0x%llx R14: 0x%llx R15: 0x%llx\n", ctxt->r12, ctxt->r13, ctxt->r14, ctxt->r15);
    kprintf("CR2: 0x%llx\n", cr2);

    cs = ctxt->cs;
    bp = ctxt->bp;

#ifdef CONFIG_DEBUG
    if (ctxt && !(cs & 0x3))
        stack_backtrace(bp);
    else {
        bp = save_bp();
        stack_backtrace(bp);
    }
#endif
}
