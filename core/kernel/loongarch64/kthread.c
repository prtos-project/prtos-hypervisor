/*
 * FILE: kthread.c
 *
 * LoongArch 64-bit kernel thread arch-dependent code
 *
 * http://www.prtos.org/
 */

#include <assert.h>
#include <rsvmem.h>
#include <gaccess.h>
#include <kthread.h>
#include <processor.h>
#include <sched.h>
#include <smp.h>
#include <spinlock.h>
#include <stdc.h>
#include <vmmap.h>
#include <arch/prtos_def.h>
#include <irqs.h>

extern void setup_stage2_mmu(kthread_t *k);
extern prtos_u32_t prtos_lvz_available;

/* GID allocator: simple counter, wraps at 255 (reserve 0 for root) */
static prtos_u32_t next_gid = 1;

void switch_kthread_arch_pre(kthread_t *new, kthread_t *current) {
    /*
     * LVZ phase 14: snapshot the per-CPU SAVE5 ("from-LVZ-guest") flag
     * into the *outgoing* thread's karch so it can be restored on
     * resumption.  Without this, scheduling away from a vmexit'd LVZ
     * guest into idle clears the flag and the eventual trap-return
     * back to the guest takes the host restore path.
     * Stored in bit 31 of guest_gid (which only uses bits 0..7).
     */
    if (prtos_lvz_available && current && current->ctrl.g &&
        current->ctrl.g->karch.lvz_enabled) {
        prtos_u64_t s5;
        __asm__ __volatile__("csrrd %0, 0x35" : "=r"(s5));
        if (s5) {
            current->ctrl.g->karch.guest_gid |= (1u << 31);
        } else {
            current->ctrl.g->karch.guest_gid &= ~(1u << 31);
        }
    }
    if (new->ctrl.g) {
        setup_stage2_mmu(new);

        /* For LVZ: set GSTAT.GID for the new partition */
        if (prtos_lvz_available && new->ctrl.g->karch.lvz_enabled) {
            prtos_u64_t gstat;
            prtos_u64_t gid = (prtos_u64_t)(new->ctrl.g->karch.guest_gid & 0xFF);
            __asm__ __volatile__("csrrd %0, 0x50" : "=r"(gstat));
            gstat &= ~(0xFFUL << 16);
            gstat |= (gid << 16);
            __asm__ __volatile__("csrwr %0, 0x50" : "+r"(gstat));
        }
    }
}

void switch_kthread_arch_post(kthread_t *current) {
    /*
     * Restore the per-thread SAVE5 snapshot so a vmexit'd LVZ guest
     * resumes via _trap_restore_guest after schedule()→idle→schedule().
     * For non-LVZ next thread, clear SAVE5/GSTAT.PVM so we don't
     * accidentally re-enter guest mode.
     */
    if (!prtos_lvz_available) {
        return;
    }
    if (current && current->ctrl.g && current->ctrl.g->karch.lvz_enabled) {
        prtos_u64_t s5 = (current->ctrl.g->karch.guest_gid >> 31) & 1;
        __asm__ __volatile__("csrwr %0, 0x35" : "+r"(s5));
        return;
    }
    __asm__ __volatile__("csrwr $zero, 0x35" ::: "memory"); /* Clear SAVE5 */
    /* Clear GSTAT.PVM to prevent accidental guest entry */
    {
        prtos_u64_t mask = 0x2;
        __asm__ __volatile__("csrxchg $zero, %0, 0x50" : : "r"(mask));
    }
}

extern void kthread_startup_wrapper(void);

void setup_kstack(kthread_t *k, void *start_up, prtos_address_t entry_point) {
    prtos_u64_t *sp = (prtos_u64_t *)(&k->kstack[CONFIG_KSTACK_SIZE]);
    /* Build a 96-byte stack frame matching the CONTEXT_SWITCH restore layout:
     * offset  0: fp  (r22)
     * offset  8: s0  (r23) = start_up
     * offset 16: s1  (r24) = entry_point
     * offset 24: s2  (r25)
     * ...
     * offset 72: s8  (r31)
     * offset 80: ra  (r1) = kthread_startup_wrapper
     * offset 88: padding
     */
    sp -= 12; /* 96 bytes / 8 = 12 slots */
    sp[0]  = 0ULL;                                    /* fp  (r22) */
    sp[1]  = (prtos_u64_t)start_up;                   /* s0  (r23) */
    sp[2]  = (prtos_u64_t)entry_point;                /* s1  (r24) */
    sp[3]  = 0ULL;                                    /* s2  (r25) */
    sp[4]  = 0ULL;                                    /* s3  (r26) */
    sp[5]  = 0ULL;                                    /* s4  (r27) */
    sp[6]  = 0ULL;                                    /* s5  (r28) */
    sp[7]  = 0ULL;                                    /* s6  (r29) */
    sp[8]  = 0ULL;                                    /* s7  (r30) */
    sp[9]  = 0ULL;                                    /* s8  (r31) */
    sp[10] = (prtos_u64_t)kthread_startup_wrapper;    /* ra  (r1)  */
    sp[11] = 0ULL;                                    /* padding   */
    k->ctrl.kstack = (prtos_address_t *)sp;
}

void kthread_arch_init(kthread_t *k) {
}

void setup_kthread_arch(kthread_t *k) {
    /* Initialize guest CSR state for hw-virt partitions */
    if (k->ctrl.g) {
        struct kthread_arch *ka = &k->ctrl.g->karch;
        ka->guest_crmd = 0;      /* PLV0, IE=0, DA mode */
        ka->guest_prmd = 0;
        ka->guest_euen = 0;
        ka->guest_misc = 0;
        ka->guest_ecfg = 0;
        ka->guest_estat = 0;
        ka->guest_era = 0;
        ka->guest_badv = 0;
        ka->guest_badi = 0;
        ka->guest_eentry = 0;
        ka->guest_timer_active = 0;
        ka->guest_tcfg = 0;
        ka->guest_tcfg_deadline = 0;
        ka->guest_in_tlb_refill = 0;
        ka->guest_tlbrentry = 0;
        ka->guest_asid = 0;

        /* Initialize PRCFG registers from real hardware */
        {
            extern void init_guest_prcfg(struct kthread_arch *ka);
            init_guest_prcfg(ka);
        }

        /* Check if this partition uses hw-virt and LVZ is available */
        {
            partition_t *p = get_partition(k);
            if (prtos_lvz_available && p && p->cfg &&
                (p->cfg->flags & PRTOS_PART_HWVIRT)) {
                ka->lvz_enabled = 1;
                ka->guest_gid = next_gid;
                next_gid = (next_gid % 255) + 1;
                kprintf("[PRTOS] Partition %d: LVZ enabled, GID=%d\n",
                        KID2PARTID(k->ctrl.g->id), ka->guest_gid);
            } else {
                ka->lvz_enabled = 0;
                ka->guest_gid = 0;
            }
        }
    }
}

void setup_pct_arch(partition_control_table_t *part_ctrl_table, kthread_t *k) {
    /* LoongArch: initialize PCT arch fields */
    part_ctrl_table->arch.trap_entry = 0;
    part_ctrl_table->arch.irq_vector = 0;
    part_ctrl_table->arch.irq_saved_pc = 0;
    part_ctrl_table->arch.irq_saved_crmd = 0;
    part_ctrl_table->arch.irq_saved_a0 = 0;

    /* Report LVZ status */
    if (k && k->ctrl.g) {
        kprintf("P%d LVZ=%d (avail=%d, gid=%d)\n",
                KID2PARTID(k->ctrl.g->id),
                k->ctrl.g->karch.lvz_enabled,
                prtos_lvz_available,
                k->ctrl.g->karch.guest_gid);
    }
}
