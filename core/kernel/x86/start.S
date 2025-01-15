/*
 * FILE: start.S
 *
 * The execution of prtos starts from this file (start symbol)
 *
 * www.prtos.org
 */

#include <config.h>
#include <linkage.h>
#include <prtosef.h>
#include <hypercalls.h>
#include <arch/asm_offsets.h>
#include <arch/irqs.h>
#include <arch/paging.h>
#include <arch/processor.h>
#include <arch/segments.h>
#include <arch/prtos_def.h>

.section .boot.text, "ax"

ASM_ALIGN

ENTRY(start)
ENTRY(_start)
        xorl %eax, %eax
        movl $_VIRT2PHYS(_sbss), %edi
        movl $_VIRT2PHYS(_ebss), %ecx
        subl %edi, %ecx
        rep
        stosb

        movl $_VIRT2PHYS(_sdata), %esi /* Backup data for cold reset */
        movl $_VIRT2PHYS(_edata), %ecx
        movl $_VIRT2PHYS(_scdata), %edi
        movl $_VIRT2PHYS(_cpdata), %eax
        subl %esi, %ecx
        mov (%eax), %ebx
        testl %ebx, %ebx
        jz 1f

        movl $_VIRT2PHYS(_scdata), %esi
        movl $_VIRT2PHYS(_sdata), %edi

1:      rep movsb
        movl $1, (%eax)

        lgdt _gdt_desc
        
        ljmp $(EARLY_CS_SEL), $1f
1:      mov $(EARLY_DS_SEL), %eax
        mov %eax, %ds
        mov %eax, %es
        mov %eax, %ss
        xorl %eax, %eax
        mov %eax, %fs
        mov %eax, %gs
       
        mov $_VIRT2PHYS(__idle_kthread)+CONFIG_KSTACK_SIZE, %esp
#ifndef CONFIG_NO_GCC_OPT
        call boot_detect_cpu_feature

        mov %eax, _VIRT2PHYS(cpu_features)
        
        andl $_DETECTED_I586|_PSE_SUPPORT|_PGE_SUPPORT, %eax
        xorl $_DETECTED_I586|_PSE_SUPPORT|_PGE_SUPPORT, %eax

        jnz __boot_halt_system
#endif
        movl $_VIRT2PHYS(_page_tables), %eax
        movl $_VIRT2PHYS(_estack), %esp
        call boot_init_page_table

#ifdef CONFIG_SMP
        movb $0x0, _VIRT2PHYS(ap_ready)
smp_start32:
#endif

        movl $(_CR4_PSE|_CR4_PGE),%eax
        mov %eax, %cr4

        movl $_VIRT2PHYS(_page_tables), %eax
        movl %eax, %cr3
        movl %cr0,%eax
        orl $0x80000000,%eax
        movl %eax,%cr0
        jmp start_prtos
        
__boot_halt_system:
        cli
        hlt
        jmp __boot_halt_system

PAGE_ALIGN
ENTRY(_reset2)
        mov    0x4(%esp),%edx
        cli
        mov    %cr0,%eax
        and    $0x7fffffff,%eax
        mov    %eax,%cr0
        xor    %eax,%eax
        mov    %eax,%cr3
        jmp    *%edx

#ifdef CONFIG_SMP
.code16
	
ENTRY(smp_start16)
rbase=.
        wbinvd          # Needed for NUMA-Q should be harmless for others
        mov %cs, %ax    # Code and data in the same place
        mov %ax, %ds

        cli             # We should be safe anyway

        lidtl smp_idt_descr-rbase        # load idt with 0, 0
        lgdtl smp_gdt_descr-rbase        # load gdt with whatever is appropriate
        xor %ax, %ax
        inc %ax                        # protected mode (PE) bit
        lmsw %ax                       # into protected mode
        ljmpl $(EARLY_CS_SEL), $1f

.code32
1:
        mov $(EARLY_DS_SEL), %eax
        mov %eax, %ds
        mov %eax, %es
        xorl %eax, %eax
        mov %eax, %fs
        mov %eax, %gs

        /* Setup an initial stack pointer */
        mov $_smp_stack+64, %esp

        ljmp $(EARLY_CS_SEL), $1f
1:
        jmp smp_start32

smp_gdt_descr:
        .word EARLY_PRTOS_GDT_ENTRIES*8-1
        .long _VIRT2PHYS(early_gdt_table)

smp_idt_descr:
        .word 0            # idt limit = 0
        .long 0            # idt base = 0L
ENTRY(smp_start16_end)
#endif        /*CONFIG_SMP*/
        
.section .boot.data, "ax"
        .word 0
_reset_addr:
        .long 0
_gdt_desc:
        .word EARLY_PRTOS_GDT_ENTRIES*8-1
        .long _VIRT2PHYS(early_gdt_table)
_smp_stack:
        .zero 64

.bss
.align 1024
.globl _stack
_stack:
	.zero (1024)
.globl _estack
_estack:


.section .kbuild_info, "a"
.ascii "BUILD_TIME="TO_STR(BUILD_TIME)"\n"
.ascii "BUILD_IDR="TO_STR(BUILD_IDR)"\n"
.previous
