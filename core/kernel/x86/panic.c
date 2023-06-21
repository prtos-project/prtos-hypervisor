/*
 * FILE: panic.c
 *
 * Code executed in a panic situation
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
#if 0
    prtos_s32_t e=1;
    SmpPrintf("Stack backtrace:\n   ", 0);
    while (bp) {
	kprintf("[0x%x] ", *(prtos_word_t *)(bp+4));
	if (!(e%5)) kprintf("\n   ");
	bp=*(prtos_word_t *)bp;
	e++;
    }
    kprintf("\n");
#endif
}
#endif

void dump_state(cpu_ctxt_t *ctxt) {
    prtos_word_t bp, cr2;  //, *sp;
    prtos_u16_t cs;

    kprintf("CPU state:\n");
    cr2 = save_cr2();
    kprintf("EIP: 0x%x:[<0x%x>]", 0xffff & ctxt->cs, ctxt->ip);
    if (ctxt->cs & 0x3)
        kprintf(" ESP: 0x%x:[<0x%x>]", 0xffff & ctxt->ss, ctxt->sp);
    else {
        prtos_word_t cSp = save_stack();
        prtos_u16_t cSs;
        save_ss(cSs);
        kprintf(" ESP: 0x%x:[<0x%x>]", 0xffff & cSs, cSp);
    }
    kprintf(" EFLAGS: 0x%x  \n", ctxt->flags);
    kprintf("EAX: 0x%x EBX: 0x%x ECX: 0x%x EDX: 0x%x\n", ctxt->ax, ctxt->bx, ctxt->cx, ctxt->dx);
    kprintf("ESI: 0x%x EDI: 0x%x EBP: 0x%x\n", ctxt->si, ctxt->di, ctxt->bp);
    kprintf("CR2: 0x%x\n", cr2);

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
