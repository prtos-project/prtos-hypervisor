/*
 * FILE: kthread.c
 *
 * Kernel, Guest context (ARCH dependent part)
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

void switch_kthread_arch_pre(kthread_t *new, kthread_t *current) {
#ifdef CONFIG_AARCH64
    if (!new->ctrl.g) {
        /* Switching away from a hw-virt partition to idle:
         * 1) Save GICv3 virtual CPU interface state (ICH_LR + control regs)
         * 2) Disable virtual interface & stage-2 MMU */
        if (current->ctrl.g && current->ctrl.g->karch.vgic) {
            prtos_u64_t v0, v1, v2, v3, vh, vm;
            asm volatile("mrs %0, S3_4_C12_C12_0\n\t"  /* ICH_LR0_EL2 */
                         "mrs %1, S3_4_C12_C12_1\n\t"  /* ICH_LR1_EL2 */
                         "mrs %2, S3_4_C12_C12_2\n\t"  /* ICH_LR2_EL2 */
                         "mrs %3, S3_4_C12_C12_3\n\t"  /* ICH_LR3_EL2 */
                         "mrs %4, S3_4_C12_C11_0\n\t"  /* ICH_HCR_EL2 */
                         "mrs %5, S3_4_C12_C11_7\n\t"  /* ICH_VMCR_EL2 */
                         : "=r"(v0), "=r"(v1), "=r"(v2), "=r"(v3),
                           "=r"(vh), "=r"(vm));
            current->ctrl.g->karch.ich_lr[0] = v0;
            current->ctrl.g->karch.ich_lr[1] = v1;
            current->ctrl.g->karch.ich_lr[2] = v2;
            current->ctrl.g->karch.ich_lr[3] = v3;
            current->ctrl.g->karch.ich_hcr  = vh;
            current->ctrl.g->karch.ich_vmcr = vm;
            /* Disable virtual CPU interface and clear LRs so idle
             * doesn't receive spurious maintenance interrupts. */
            asm volatile("msr S3_4_C12_C11_0, xzr\n\t"  /* ICH_HCR_EL2 = 0 */
                         "msr S3_4_C12_C12_0, xzr\n\t"
                         "msr S3_4_C12_C12_1, xzr\n\t"
                         "msr S3_4_C12_C12_2, xzr\n\t"
                         "msr S3_4_C12_C12_3, xzr\n\t"
                         "isb" ::: "memory");
        }

        /* Disable stage-2 MMU */
        asm volatile("msr vttbr_el2, xzr\n\t"
                     "mrs x10, hcr_el2\n\t"
                     "bic x10, x10, #1\n\t" /* clear VM bit */
                     "msr hcr_el2, x10\n\t"
                     "isb" ::
                         : "x10", "memory");
    }
#endif
}

void switch_kthread_arch_post(kthread_t *current) {
#ifdef CONFIG_AARCH64
    if (current->ctrl.g) {
        /* Restore per-partition stage-2 MMU */
        prtos_u64_t vttbr = current->ctrl.g->karch.vttbr;
        if (vttbr) {
            __asm__ __volatile__(
                "msr vttbr_el2, %0\n\t"
                "dsb ish\n\t"
                "tlbi alle1is\n\t"
                "dsb ish\n\t"
                "isb\n\t"
                :
                : "r"(vttbr)
                : "memory");

            /* Set HCR_EL2: RW | AMO | IMO | FMO | VM = 0x8000_0039 */
            prtos_u64_t hcr = PRTOS_HCR_EL2_VAL;
            __asm__ __volatile__(
                "msr hcr_el2, %0\n\t"
                "isb\n\t"
                :
                : "r"(hcr)
                : "memory");

            /* Restore GICv3 virtual CPU interface state for hw-virt partitions.
             * Restores ICH_LR0-3 saved in switch_kthread_arch_pre, then
             * re-enables ICH_HCR_EL2 and ICH_VMCR_EL2. Finally flushes
             * any newly pending VGIC IRQs (SGIs, timer) accumulated while
             * this vCPU was preempted. */
            if (current->ctrl.g->karch.vgic) {
                prtos_u64_t lr0 = current->ctrl.g->karch.ich_lr[0];
                prtos_u64_t lr1 = current->ctrl.g->karch.ich_lr[1];
                prtos_u64_t lr2 = current->ctrl.g->karch.ich_lr[2];
                prtos_u64_t lr3 = current->ctrl.g->karch.ich_lr[3];
                prtos_u64_t hcr = current->ctrl.g->karch.ich_hcr;
                prtos_u64_t vmcr = current->ctrl.g->karch.ich_vmcr;
                /* Default values for first run (before any save) */
                if (!hcr) hcr = 0x1;
                if (!vmcr) vmcr = 0xFF000002ULL;
                __asm__ __volatile__(
                    "msr S3_4_C12_C12_0, %0\n\t"  /* ICH_LR0_EL2 */
                    "msr S3_4_C12_C12_1, %1\n\t"
                    "msr S3_4_C12_C12_2, %2\n\t"
                    "msr S3_4_C12_C12_3, %3\n\t"
                    "msr S3_4_C12_C11_0, %4\n\t"   /* ICH_HCR_EL2 */
                    "msr S3_4_C12_C11_7, %5\n\t"   /* ICH_VMCR_EL2 */
                    "isb\n\t"
                    :
                    : "r"(lr0), "r"(lr1), "r"(lr2), "r"(lr3),
                      "r"(hcr), "r"(vmcr)
                    : "memory");

                /* Timer virtualization */
                __asm__ __volatile__(
                    "msr CNTVOFF_EL2, xzr\n\t"
                    "mov x10, #1\n\t"           /* EL1PCTEN = 1 */
                    "msr CNTHCTL_EL2, x10\n\t"
                    "isb\n\t"
                    ::: "x10", "memory");

                /* Flush any newly pending VGIC IRQs into free LR slots */
                extern void prtos_vgic_flush_lrs_current(void);
                extern void prtos_virtio_console_poll(void);
                prtos_virtio_console_poll();
                prtos_vgic_flush_lrs_current();
            } else {
                /* Para-virt partition: just enable virtual interface */
                __asm__ __volatile__(
                    "msr S3_4_C12_C11_0, %0\n\t"
                    "msr S3_4_C12_C11_7, %1\n\t"
                    "isb\n\t"
                    :
                    : "r"((prtos_u64_t)0x1), "r"((prtos_u64_t)0xFF000002ULL)
                    : "memory");
            }
        }
    }
#endif
}

extern void kthread_startup_wrapper(void);

extern void setup_kstack(kthread_t *k, void *start_up, prtos_address_t entry_point) {
    prtos_u64_t *sp = (prtos_u64_t *)(&k->kstack[CONFIG_KSTACK_SIZE]);
    /* Build a stack frame matching the CONTEXT_SWITCH restore sequence:
     * ldp x19, x20 / ldp x21, x22 / ldp x23, x24 /
     * ldp x25, x26 / ldp x27, x28 / ldp x29(fp), x30(lr)
     * x19 = start_up, x20 = entry_point, x30 = kthread_startup_wrapper
     */
    *(--sp) = (prtos_u64_t)kthread_startup_wrapper; /* lr (x30) */
    *(--sp) = 0ULL;                                 /* fp (x29) */
    *(--sp) = 0ULL;
    *(--sp) = 0ULL; /* x28, x27 */
    *(--sp) = 0ULL;
    *(--sp) = 0ULL; /* x26, x25 */
    *(--sp) = 0ULL;
    *(--sp) = 0ULL; /* x24, x23 */
    *(--sp) = 0ULL;
    *(--sp) = 0ULL;                     /* x22, x21 */
    *(--sp) = (prtos_u64_t)entry_point; /* x20 */
    *(--sp) = (prtos_u64_t)start_up;    /* x19 */
    k->ctrl.kstack = (prtos_address_t *)sp;
}

void kthread_arch_init(kthread_t *k) {
    // if (are_kthread_flags_set(k, KTHREAD_FP_F)) {
    //     load_cr0(save_cr0() & (~(_CR0_EM | _CR0_TS)));
    //     FNINIT();
    //     save_fpu_state(k->ctrl.g->karch.fp_ctxt);
    // }

    // k->ctrl.g->karch.tss.t.ss0 = DS_SEL;
    // k->ctrl.g->karch.tss.t.sp0 = (prtos_address_t)&k->kstack[CONFIG_KSTACK_SIZE];
    // k->ctrl.g->karch.gdt_table[PERCPU_SEL >> 3] = gdt_table[GDT_ENTRY(GET_CPU_ID(), PERCPU_SEL)];
    // set_wp();
}

void setup_kthread_arch(kthread_t *k) {
    // partition_t *p = get_partition(k);

    // ASSERT(k->ctrl.g);
    // ASSERT(p);
    // memcpy(k->ctrl.g->karch.gdt_table, gdt_table, sizeof(struct x86_desc) * (PRTOS_GDT_ENTRIES + CONFIG_PARTITION_NO_GDT_ENTRIES));

    // k->ctrl.g->karch.gdtr.limit = (sizeof(struct x86_desc) * (CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES)) - 1;
    // k->ctrl.g->karch.gdtr.linear_base = (prtos_address_t)k->ctrl.g->karch.gdt_table;

    // k->ctrl.g->karch.cr0 = _CR0_PE | _CR0_PG;
    // if (!are_kthread_flags_set(k, KTHREAD_FP_F)) {
    //     k->ctrl.g->karch.cr0 |= _CR0_EM;
    // }

    // memcpy(k->ctrl.g->karch.hyp_idt_table, hyp_idt_table, sizeof(struct x86_gate) * IDT_ENTRIES);

    // k->ctrl.g->karch.idtr.limit = (sizeof(struct x86_gate) * IDT_ENTRIES) - 1;
    // k->ctrl.g->karch.idtr.linear_base = (prtos_address_t)k->ctrl.g->karch.hyp_idt_table;

    // if (p->cfg->num_of_io_ports > 0) {
    //     memcpy(k->ctrl.g->karch.tss.io_map, prtos_conf_io_port_table[p->cfg->io_ports_offset].map, 2048 * sizeof(prtos_u32_t));
    //     enable_tss_io_map(&k->ctrl.g->karch.tss);
    // } else
    //     disable_tss_io_map(&k->ctrl.g->karch.tss);
    // load_tss_desc(&k->ctrl.g->karch.gdt_table[TSS_SEL >> 3], &k->ctrl.g->karch.tss);
}

void setup_pct_arch(partition_control_table_t *part_ctrl_table, kthread_t *k) {
    // prtos_s32_t e;

    // part_ctrl_table->arch.cr3 = k->ctrl.g->karch.ptd_level_1;
    // part_ctrl_table->arch.cr0 = _CR0_PE | _CR0_PG;
    // if (!are_kthread_flags_set(k, KTHREAD_FP_F)) {
    //     part_ctrl_table->arch.cr0 |= _CR0_EM;
    // }

    // for (e = 0; e < NO_TRAPS; e++) k->ctrl.g->part_ctrl_table->trap_to_vector[e] = e;

    // for (e = 0; e < CONFIG_NO_HWIRQS; e++) k->ctrl.g->part_ctrl_table->hw_irq_to_vector[e] = e + FIRST_EXTERNAL_VECTOR;

    // for (e = 0; e < PRTOS_VT_EXT_MAX; e++) k->ctrl.g->part_ctrl_table->ext_irq_to_vector[e] = 0x90 + e;
}
