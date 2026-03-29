/*
 * FILE: traps.c
 *
 * RISC-V 64-bit trap handler (C part)
 *
 * www.prtos.org
 */

#include <assert.h>
#include <irqs.h>
#include <kthread.h>
#include <ktimer.h>
#include <processor.h>
#include <sched.h>
#include <stdc.h>
#include <hypercalls.h>

extern void do_hyp_irq(cpu_ctxt_t *ctxt);
extern void do_hyp_trap(cpu_ctxt_t *ctxt);
extern prtos_s32_t raise_pend_irqs(cpu_ctxt_t *ctxt);

struct cpu_user_regs *prtos_current_guest_regs_percpu[CONFIG_NO_CPUS];

/* SBI timer set (SBI call to M-mode) */
static void sbi_set_timer(prtos_u64_t stime_value) {
    register prtos_u64_t a0 __asm__("a0") = stime_value;
    register prtos_u64_t a7 __asm__("a7") = 0;  /* SBI_SET_TIMER (legacy) */
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

/* Read the RISC-V time CSR */
static inline prtos_u64_t read_time(void) {
    prtos_u64_t val;
    __asm__ __volatile__("rdtime %0" : "=r"(val));
    return val;
}

/* Forward declaration of hypercall handler */
extern prtos_s32_t do_hypercall(prtos_u32_t hc_nr, prtos_u64_t a1, prtos_u64_t a2,
                                prtos_u64_t a3, prtos_u64_t a4, prtos_u64_t a5);

/* Forward declaration of SBI emulation handler */
extern int prtos_sbi_handle(struct cpu_user_regs *regs);

/*
 * riscv64_trap_handler - main C trap handler
 *
 * Called from entry.S _trap_entry with pointer to saved register frame.
 */
void riscv64_trap_handler(struct cpu_user_regs *regs) {
    prtos_u64_t scause = regs->scause;
    prtos_u64_t is_interrupt = scause & (1ULL << 63);
    prtos_u64_t exception_code = scause & ~(1ULL << 63);

    /* Save regs pointer for virtual IRQ delivery (fix_stack) */
    prtos_current_guest_regs_percpu[GET_CPU_ID()] = regs;

    if (is_interrupt) {
        /* Interrupt */
        switch (exception_code) {
        case 1:  /* Supervisor software interrupt (IPI) */
            /* Clear SSIP */
            __asm__ __volatile__("csrc sip, %0" : : "r"(1UL << 1));
            /* If current guest has a pending VSSIP from SBI IPI, inject it */
            {
                local_processor_t *info = GET_LOCAL_PROCESSOR();
                kthread_t *k = info->sched.current_kthread;
                if (k && k->ctrl.g && k->ctrl.g->karch.vssip_pending) {
                    k->ctrl.g->karch.vssip_pending = 0;
                    __asm__ __volatile__("csrs hvip, %0" :: "r"(1UL << 2));
                }
            }
            {
                cpu_ctxt_t ctxt;
                ctxt.irq_nr = 1;
                do_hyp_irq(&ctxt);
            }
            break;
        case 5: {  /* Supervisor timer interrupt */
            /* Acknowledge: schedule next timer far away to clear pending */
            sbi_set_timer((prtos_u64_t)-1);
            /* Clear timer interrupt pending */
            __asm__ __volatile__("csrc sip, %0" : : "r"(1UL << 5));

            /* Debug: periodic guest PC dump */
            {
                static unsigned long tmr_cnt = 0;
                tmr_cnt++;
                if (tmr_cnt == 1 || tmr_cnt == 100 || tmr_cnt == 1000 ||
                    tmr_cnt == 10000 || tmr_cnt == 100000 || (tmr_cnt % 500000 == 0)) {
                    kprintf("[TMR] cpu%d #%lu sepc=0x%llx\n", GET_CPU_ID(), tmr_cnt, regs->sepc);
                }
            }

            /* If a guest partition uses SBI timer (hw-virt), inject VSTIP
             * (Virtual Supervisor Timer Interrupt Pending) via hvip so the
             * guest's vstvec handler is invoked on sret. The guest will
             * re-program the timer in its tick handler via SBI set_timer. */
            {
                local_processor_t *info = GET_LOCAL_PROCESSOR();
                kthread_t *k = info->sched.current_kthread;
                if (k && k->ctrl.g && k->ctrl.g->karch.guest_timer_active) {
                    __asm__ __volatile__("csrs hvip, %0" :: "r"(1UL << 6));
                }
                /* Also re-inject any pending VSSIP that may have been missed
                 * by the IPI handler due to timing races. */
                if (k && k->ctrl.g && k->ctrl.g->karch.vssip_pending) {
                    k->ctrl.g->karch.vssip_pending = 0;
                    __asm__ __volatile__("csrs hvip, %0" :: "r"(1UL << 2));
                }
            }

            /* Call the ktimer handler registered via set_timer_handler */
            {
                extern timer_handler_t riscv_timer_handler;
                if (riscv_timer_handler)
                    riscv_timer_handler();
            }
            /* Call the common IRQ handler which dispatches to registered handler */
            {
                cpu_ctxt_t ctxt;
                ctxt.irq_nr = 5;
                do_hyp_irq(&ctxt);
            }
            break;
        }
        case 9:  /* Supervisor external interrupt */
            break;
        default:
            break;
        }
    } else {
        /* Exception (synchronous trap) */
        switch (exception_code) {
        case 9:  /* ECALL from S-mode (guest hypercall via para-virt) */
        case 10: { /* ECALL from VS-mode (guest hypercall via H-extension) */
            prtos_u32_t hc_nr;
            prtos_s32_t result;
            /* Advance sepc past the ecall instruction (4 bytes) */
            regs->sepc += 4;

            /* Check for new-style SBI extensions (a7 != 0 means extension ID).
             * Route to SBI emulation handler for HSM, IPI, RFENCE, BASE, TIME. */
            if (regs->a7 != 0 && prtos_sbi_handle(regs)) {
                break;
            }

            /* Check for SBI legacy set_timer call (a7 == 0, a0 = timer value):
             * Native (unmodified) guests use SBI ecalls for timer.
             * Detect via a7 == 0 (SBI extension ID for legacy set_timer)
             * AND a0 > NR_HYPERCALLS + 1 (SBI timer values are large numbers,
             * while PRTOS hypercall numbers are 0..NR_HYPERCALLS+1).
             * This avoids false positives when BAIL partitions do ecall with
             * a7 uninitialized (possibly 0) and a0 = hypercall number. */
            if (regs->a7 == 0 && regs->a0 > (NR_HYPERCALLS + 1)) {
                /* SBI legacy set_timer: a0 = stime_value */
                local_processor_t *info = GET_LOCAL_PROCESSOR();
                kthread_t *k = info->sched.current_kthread;
                if (k && k->ctrl.g) {
                    k->ctrl.g->karch.guest_timer_active = 1;
                    /* Clear any pending VSTIP before reprogramming */
                    __asm__ __volatile__("csrc hvip, %0" :: "r"(1UL << 6));
                    /* Forward to real SBI to program hardware timer */
                    sbi_set_timer(regs->a0);
                }
                break;
            }

            hc_nr = (prtos_u32_t)regs->a0;

            /* Handle IRET: guest returns from virtual IRQ */
            if (hc_nr == NR_HYPERCALLS) {
                local_processor_t *info = GET_LOCAL_PROCESSOR();
                kthread_t *k = info->sched.current_kthread;
                if (k && k->ctrl.g) {
                    partition_control_table_t *pct = k->ctrl.g->part_ctrl_table;
                    if (pct->arch.irq_vector) {
                        regs->sepc = pct->arch.irq_saved_pc;
                        regs->sstatus = pct->arch.irq_saved_sstatus;
                        regs->a0 = pct->arch.irq_saved_a0;
                        pct->arch.irq_vector = 0;
                    }
                }
                break;
            }

            /* Handle RAISE_TRAP: partition raises a trap to itself */
            if (hc_nr == NR_HYPERCALLS + 1) {
                cpu_ctxt_t trap_ctxt;
                trap_ctxt.irq_nr = (prtos_u64_t)regs->a1;
                trap_ctxt.pc = regs->sepc;
                trap_ctxt.sp = 0;
                do_hyp_trap(&trap_ctxt);
                break;
            }

            result = do_hypercall(hc_nr, regs->a1, regs->a2,
                                  regs->a3, regs->a4, regs->a5);
            regs->a0 = (prtos_u64_t)(prtos_s64_t)result;
            break;
        }
        case 20:  /* Instruction guest-page fault (G-stage) */
        case 21:  /* Load guest-page fault (G-stage) */
        case 23: { /* Store/AMO guest-page fault (G-stage) */
            /* Map scause to partition trap number for HM dispatch:
             * 20 → RISCV64_INSTR_PAGE_FAULT (4)
             * 21 → RISCV64_LOAD_PAGE_FAULT  (5)
             * 23 → RISCV64_STORE_PAGE_FAULT (6) */
            static const prtos_u64_t gpf_to_trap[] = {
                [20] = 4, [21] = 5, [23] = 6
            };
            cpu_ctxt_t trap_ctxt;
            trap_ctxt.irq_nr = gpf_to_trap[exception_code];
            trap_ctxt.pc = regs->sepc;
            trap_ctxt.sp = regs->stval;
            do_hyp_trap(&trap_ctxt);
            break;
        }
        case 22: { /* Virtual instruction exception */
            /* Emulate: skip the instruction (4 bytes) */
            regs->sepc += 4;
            break;
        }
        default:
            /* Unhandled trap - system panic */
            kprintf("[TRAP] UNHANDLED scause=0x%llx sepc=0x%llx stval=0x%llx\n",
                    scause, regs->sepc, regs->stval);
            halt_system();
            break;
        }
    }

    /* Deliver pending virtual IRQs before returning to guest.
     * Re-set prtos_current_guest_regs_percpu because schedule() may have
     * context-switched to a different kthread whose regs pointer differs. */
    prtos_current_guest_regs_percpu[GET_CPU_ID()] = regs;
    {
        cpu_ctxt_t pend_ctxt;
        pend_ctxt.irq_nr = 0;
        raise_pend_irqs(&pend_ctxt);
    }
}

/*
 * do_hypercall - dispatch a hypercall from the table
 */
prtos_s32_t do_hypercall(prtos_u32_t hc_nr, prtos_u64_t a1, prtos_u64_t a2,
                         prtos_u64_t a3, prtos_u64_t a4, prtos_u64_t a5) {
    extern prtos_address_t hypercalls_table[];
    extern prtos_u32_t hypercall_flags_table[];
    typedef prtos_s32_t (*hcall_0_t)(void);
    typedef prtos_s32_t (*hcall_1_t)(prtos_u64_t);
    typedef prtos_s32_t (*hcall_2_t)(prtos_u64_t, prtos_u64_t);
    typedef prtos_s32_t (*hcall_3_t)(prtos_u64_t, prtos_u64_t, prtos_u64_t);
    typedef prtos_s32_t (*hcall_4_t)(prtos_u64_t, prtos_u64_t, prtos_u64_t, prtos_u64_t);
    typedef prtos_s32_t (*hcall_5_t)(prtos_u64_t, prtos_u64_t, prtos_u64_t, prtos_u64_t, prtos_u64_t);

    if (hc_nr >= NR_HYPERCALLS) return -1;

    prtos_address_t handler = hypercalls_table[hc_nr];
    if (!handler) return -1;

    prtos_u32_t flags = hypercall_flags_table[hc_nr];
    prtos_u32_t nargs = flags & 0xF;

    switch (nargs) {
    case 0: return ((hcall_0_t)handler)();
    case 1: return ((hcall_1_t)handler)(a1);
    case 2: return ((hcall_2_t)handler)(a1, a2);
    case 3: return ((hcall_3_t)handler)(a1, a2, a3);
    case 4: return ((hcall_4_t)handler)(a1, a2, a3, a4);
    case 5: return ((hcall_5_t)handler)(a1, a2, a3, a4, a5);
    default: return -1;
    }
}
