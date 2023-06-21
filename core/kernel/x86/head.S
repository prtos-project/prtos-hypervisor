/*
 * FILE: head.S
 *
 * Entry point of the PRTOS in the x86 architecture version
 *
 * www.prtos.org
*/

#include <linkage.h>
#include <arch/irqs.h>
#include <arch/asm_offsets.h>
#include <arch/segments.h>
#include <arch/prtos_def.h>

.section .vboot.text, "ax"
.code32

ENTRY(start_prtos)
        lidt idt_desc

        lss _sstack, %esp

        call setup_cr
        call setup_gdt

#ifdef CONFIG_SMP
        movb ap_ready, %al
        cmpb $0x0, %al
        je 1f

        cld
        pushl $0
        popf
        orb $0x80, ap_ready
#ifdef CONFIG_DEBUG
        movl $0, %ebp
#endif /*CONFIG_DEBUG*/

        call init_secondary_cpu  /* Application CPU does not come back */

1:      movb $0x1, ap_ready
#endif /*CONFIG_SMP*/
        push $__idle_kthread /* Idle kthread */
        pushl $0             /* CPU 0 ID     */
        call setup_kernel

.text
ENTRY(__halt_system)
        cli
        hlt
        jmp __halt_system

.data
PAGE_ALIGN
        .word 0
ENTRY(idt_desc)
        .word IDT_ENTRIES*8-1
        .long _VIRT2PHYS(hyp_idt_table)
        
#ifdef CONFIG_SMP
.section .vboot.data, "ax"
ENTRY(ap_ready)
        .byte 0
#endif /*CONFIG_SMP*/
ENTRY(_sstack)
		.long __idle_kthread+CONFIG_KSTACK_SIZE
        .word EARLY_DS_SEL
.data
ENTRY(early_gdt_table)
        .quad 0x0000000000000000 /* NULL  descriptor */
        .quad 0x00cf9b000000ffff /* 1<<3 PRTOS's code segment R0 */
        .quad 0x00cf93000000ffff /* 2<<3 PRTOS's data segment R0 */


.bss
PAGE_ALIGN
ENTRY(_page_tables)
        .zero 2*PAGE_SIZE

ENTRY(prtos_reserve_one_phys_page)
        .zero PAGE_SIZE

ENTRY(hyp_idt_table)
        .zero IDT_ENTRIES*8
        
ENTRY(__idle_kthread)
        .zero CONFIG_KSTACK_SIZE*CONFIG_NO_CPUS
