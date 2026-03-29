/*
 * FILE: start.S
 *
 * Entry point for amd64 PRTOS - transitions from 32-bit to 64-bit long mode
 *
 * www.prtos.org
 */

#include <config.h>
#include <linkage.h>
#include <prtosef.h>
#include <hypercalls.h>
#include <arch/irqs.h>
#include <arch/paging.h>
#include <arch/processor.h>
#include <arch/segments.h>
#include <arch/prtos_def.h>

/* We enter in 32-bit protected mode (from multiboot/RSW) */
.section .boot.text, "ax"
.code32

ASM_ALIGN

ENTRY(start)
ENTRY(_start)
        cli

        /* Clear BSS */
        movl $_VIRT2PHYS(_sbss), %edi
        movl $_VIRT2PHYS(_ebss), %ecx
        subl %edi, %ecx
        xorl %eax, %eax
        rep stosb

        /* Backup/restore data section for cold/warm reset */
        movl $_VIRT2PHYS(_sdata), %esi
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
        movl $_VIRT2PHYS(_cpdata), %eax
        movl $1, (%eax)

        /* Set up a temporary stack */
        movl $_VIRT2PHYS(__idle_kthread)+CONFIG_KSTACK_SIZE, %esp

        /* Set up identity-mapped page tables for transition to long mode
         * Done in assembly since we're in 32-bit mode but C code is 64-bit.
         * _page_tables has 6 pages:
         *   page 0: PML4
         *   page 1: PDPT (low identity mapping)
         *   page 2: PD (low identity mapping)
         *   page 3: PDPT (high virtual mapping)
         *   page 4: PD (high virtual mapping)
         *   page 5: reserved
         */
        /* Clear all 6 pages */
        movl $_VIRT2PHYS(_page_tables), %edi
        xorl %eax, %eax
        movl $(6*4096/4), %ecx
        rep stosl

        movl $_VIRT2PHYS(_page_tables), %ebx  /* ebx = PML4 base */

        /* PML4[0] -> PDPT (low) */
        leal 4096(%ebx), %eax         /* eax = PDPT address */
        orl  $0x3, %eax               /* P + RW */
        movl %eax, (%ebx)
        movl $0, 4(%ebx)

        /* PDPT[0] -> PD (low) */
        leal (2*4096)(%ebx), %eax     /* eax = PD address */
        orl  $0x3, %eax
        leal 4096(%ebx), %edi         /* edi = PDPT */
        movl %eax, (%edi)
        movl $0, 4(%edi)

        /* PD: Identity map first 1GB using 2MB pages (entries 0..7 at minimum) */
        leal (2*4096)(%ebx), %edi     /* edi = PD (low) */
        xorl %eax, %eax              /* start phys addr = 0 */
        movl $512, %ecx              /* 512 entries = 1GB */
1:
        movl %eax, %edx
        orl  $(0x1|0x2|0x80), %edx    /* P + RW + PS (2MB page) */
        movl %edx, (%edi)
        movl $0, 4(%edi)
        addl $0x200000, %eax
        addl $8, %edi
        decl %ecx
        jnz  1b

        /* High mapping: PML4[idx] -> PDPT_hi, PDPT_hi[idx] -> PD_hi */
        /* PML4 index for CONFIG_PRTOS_OFFSET (0xFC000000):
         * PML4 index = (addr >> 39) & 0x1FF = 0 for addresses < 0x8000000000 */
        /* But 0xFC000000 >> 39 = 0, PDPT index = (0xFC000000 >> 30) & 0x1FF = 3 */
        /* PD index = (0xFC000000 >> 21) & 0x1FF = 0 */
        /* PML4[0] already points to PDPT (low). We can reuse it. */
        /* PDPT[3] -> PD_hi for mapping 0xC0000000-0xFFFFFFFF range */
        leal (4*4096)(%ebx), %eax     /* eax = PD_hi */
        orl  $0x3, %eax
        leal 4096(%ebx), %edi         /* edi = PDPT (low) */
        movl %eax, 24(%edi)           /* PDPT[3] -> PD_hi */
        movl $0, 28(%edi)

        /* PD_hi: map CONFIG_PRTOS_LOAD_ADDR(0x1000000) at virtual 0xFC000000 */
        /* PD_hi index = (0xFC000000 >> 21) & 0x1FF = 0x1E0 = 480 */
        leal (4*4096)(%ebx), %edi     /* edi = PD_hi */
        /* Map 8 consecutive 2MB pages: phys 0x1000000..0x1FFFFFF -> virt 0xFC000000..0xFCFFFFFF */
        movl $CONFIG_PRTOS_LOAD_ADDR, %eax
        andl $0xFFE00000, %eax         /* align to 2MB */
        movl $8, %ecx
        /* Compute PD index for CONFIG_PRTOS_OFFSET & mask */
        leal (480*8)(%edi), %edi      /* PD_hi[480] */
2:
        movl %eax, %edx
        orl  $(0x1|0x2|0x80|0x100), %edx  /* P + RW + PS + Global */
        movl %edx, (%edi)
        movl $0, 4(%edi)
        addl $0x200000, %eax
        addl $8, %edi
        decl %ecx
        jnz  2b

#ifdef CONFIG_SMP
        movb $0x0, _VIRT2PHYS(ap_ready)
smp_start32:
#endif

        /* Enable PAE (required for long mode) */
        movl %cr4, %eax
        orl $(_CR4_PAE|_CR4_PGE|_CR4_PSE), %eax
        movl %eax, %cr4

        /* Load PML4 page table */
        movl $_VIRT2PHYS(_page_tables), %eax
        movl %eax, %cr3

        /* Enable long mode via EFER MSR */
        movl $MSR_IA32_EFER, %ecx
        rdmsr
        orl $(_MSR_EFER_LME|_MSR_EFER_NXE), %eax
        wrmsr

        /* Enable paging (enters compatibility mode within long mode) */
        movl %cr0, %eax
        orl $(_CR0_PG|_CR0_PE|_CR0_WP), %eax
        movl %eax, %cr0

        /* Load 64-bit GDT and far jump to 64-bit code */
        lgdt _gdt_desc

        /* Far jump to enter 64-bit mode - use push/lret to avoid relocation issues */
        pushl $(EARLY_CS_SEL)
        movl $start64, %eax
        pushl %eax
        lret

__boot_halt_system:
        cli
        hlt
        jmp __boot_halt_system

.text
.code64
.balign 4096
ENTRY(_reset2)
        movq %rdi, %rdx    /* first arg = target address */
        cli
        movq %cr0, %rax
        andq $0x7fffffff, %rax  /* disable paging */
        movq %rax, %cr0
        xorq %rax, %rax
        movq %rax, %cr3
        jmp *%rdx

.section .boot.text, "ax"
.code64
start64:
        /* Now in 64-bit long mode */
        movl $(EARLY_DS_SEL), %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        xorl %eax, %eax
        movw %ax, %fs
        movw %ax, %gs

        /* Set up 64-bit stack from _sstack (works for both BSP and AP).
         * BSP: _sstack.rsp = __idle_kthread+CONFIG_KSTACK_SIZE (initial value)
         * AP:  _sstack.rsp = AP stack (set by setup_ap_stack, with cpu_id pushed) */
        movabsq $_sstack, %rax
        movq (%rax), %rsp

        /* Jump to virtual address space */
        movabsq $start_prtos, %rax
        jmp *%rax

#ifdef CONFIG_SMP
.code16

ENTRY(smp_start16)
rbase=.
        wbinvd
        mov %cs, %ax
        mov %ax, %ds
        cli

        lidtl smp_idt_descr-rbase
        lgdtl smp_gdt_descr-rbase
        xor %ax, %ax
        inc %ax
        lmsw %ax
        ljmpl $(EARLY_CS32_SEL), $1f

.code32
1:
        mov $(EARLY_DS_SEL), %eax
        mov %eax, %ds
        mov %eax, %es
        xorl %eax, %eax
        mov %eax, %fs
        mov %eax, %gs
        mov $_smp_stack+64, %esp
        jmp smp_start32

smp_gdt_descr:
        .word EARLY_PRTOS_GDT_ENTRIES*8-1
        .long _VIRT2PHYS(early_gdt_table)

smp_idt_descr:
        .word 0
        .long 0
ENTRY(smp_start16_end)
#endif

.section .boot.data, "ax"
        .word 0
_gdt_desc:
        .word EARLY_PRTOS_GDT_ENTRIES*8-1
        .long _VIRT2PHYS(early_gdt_table)
_smp_stack:
        .zero 64

.bss
.align 1024
.globl _stack
_stack:
        .zero (4096)
.globl _estack
_estack:

.section .kbuild_info, "a"
.ascii "BUILD_TIME="TO_STR(BUILD_TIME)"\n"
.ascii "BUILD_IDR="TO_STR(BUILD_IDR)"\n"
.previous
