/*
 * FILE: head.S
 *
 * Entry point of PRTOS in amd64 (virtual address space)
 *
 * www.prtos.org
 */

#include <linkage.h>
#include <arch/irqs.h>
#include <arch/asm_offsets.h>
#include <arch/segments.h>
#include <arch/prtos_def.h>

.section .vboot.text, "ax"
.code64

ENTRY(start_prtos)
        lidt idt_desc(%rip)

        /* Stack already set from start.S via _sstack:
         * BSP: __idle_kthread+CONFIG_KSTACK_SIZE
         * AP:  AP stack with [rsp]=cpu_id pushed by setup_ap_stack */

        call setup_cr

#ifdef CONFIG_SMP
        movb ap_ready(%rip), %al
        cmpb $0x0, %al
        jne .Lap_path
#endif

        /* BSP path */
        xorl %edi, %edi   /* cpu_id = 0 for BSP */
        call setup_gdt

#ifdef CONFIG_SMP
        movb $0x1, ap_ready(%rip)
#endif

        xorl %edi, %edi
        leaq __idle_kthread(%rip), %rsi
        call setup_kernel
        jmp __halt_system

#ifdef CONFIG_SMP
.Lap_path:
        /* AP path: read cpu_id and idle kthread from stack (pushed by setup_ap_stack).
         * Use callee-saved regs to preserve across function calls. */
        popq %rbx              /* rbx = cpu_id */
        popq %r12              /* r12 = idle kthread base */

        movl %ebx, %edi       /* setup_gdt(cpu_id) */
        call setup_gdt

        cld
        pushq $0
        popfq
        orb $0x80, ap_ready(%rip)
#ifdef CONFIG_DEBUG
        xorq %rbp, %rbp
#endif

        movl %ebx, %edi       /* init_secondary_cpu(cpu_id, idle) */
        movq %r12, %rsi
        call init_secondary_cpu
#endif

.text
ENTRY(__halt_system)
        cli
        hlt
        jmp __halt_system

.data
PAGE_ALIGN
        .word 0
ENTRY(idt_desc)
        .word IDT_ENTRIES*16-1
        .quad _VIRT2PHYS(hyp_idt_table)

#ifdef CONFIG_SMP
.section .vboot.data, "ax"
ENTRY(ap_ready)
        .byte 0
#endif

ENTRY(_sstack)
        .quad __idle_kthread+CONFIG_KSTACK_SIZE
        .word EARLY_DS_SEL

ENTRY(early_gdt_table)
        .quad 0x0000000000000000       /* NULL descriptor */
        .quad 0x00af9b000000ffff       /* 1<<3 64-bit code segment R0 (L=1, D=0) */
        .quad 0x00cf93000000ffff       /* 2<<3 data segment R0 */
        .quad 0x00cf9b000000ffff       /* 3<<3 32-bit code segment R0 (D=1, L=0) for AP boot */

.bss
PAGE_ALIGN
ENTRY(_page_tables)
        .zero 6*PAGE_SIZE  /* PML4 + PDPT + PD entries + extra */

ENTRY(prtos_reserve_one_phys_page)
        .zero PAGE_SIZE

ENTRY(hyp_idt_table)
        .zero IDT_ENTRIES*16   /* 16 bytes per entry in 64-bit mode */

ENTRY(__idle_kthread)
        .zero CONFIG_KSTACK_SIZE*CONFIG_NO_CPUS
