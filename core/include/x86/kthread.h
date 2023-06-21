/*
 * FILE: kthread.h
 *
 * Arch kernel thread
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_KTHREAD_H_
#define _PRTOS_ARCH_KTHREAD_H_

#include <irqs.h>
#include <arch/processor.h>
#include <arch/segments.h>
#include <arch/prtos_def.h>
#include <arch/atomic.h>

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

struct kthread_arch {
    prtos_address_t ptd_level_1;
    prtos_u32_t cr0;
    prtos_u32_t cr4;
    prtos_u32_t p_cpuid;

    prtos_u8_t fp_ctxt[108];
    struct x86_desc_reg gdtr;
    struct x86_desc gdt_table[CONFIG_PARTITION_NO_GDT_ENTRIES + PRTOS_GDT_ENTRIES];
    struct x86_desc_reg idtr;
    struct x86_gate hyp_idt_table[IDT_ENTRIES];
    struct io_tss tss;
};

#endif
