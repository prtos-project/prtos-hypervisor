/*
 * FILE: boot.S
 *
 * Generic boot code for amd64 RSW.
 * GRUB enters via multiboot in 32-bit protected mode.
 * We set up page tables, enable long mode, then call rsw_main in 64-bit.
 *
 * www.prtos.org
 */

#include "multiboot.h"

#define MSR_IA32_EFER 0xC0000080
#define EFER_LME      (1 << 8)
#define EFER_NXE      (1 << 11)
#define CR4_PAE       (1 << 5)
#define CR0_PG        (1 << 31)
#define CR0_PE        (1 << 0)
#define CR0_WP        (1 << 16)

/* Page table entry flags */
#define PTE_P   0x01
#define PTE_RW  0x02
#define PTE_PS  0x80

.section .multiboot
.align 4
_multiboot_header:
    .long __MULTIBOOT_HEADER_MAGIC
    .long __MULTIBOOT_HEADER_FLAGS
    .long -(__MULTIBOOT_HEADER_MAGIC+__MULTIBOOT_HEADER_FLAGS)

/*
 * Entry in 32-bit protected mode from GRUB multiboot.
 * We must transition to 64-bit long mode before calling rsw_main.
 */
.text
.code32
.align 4

.global start, _start, _halt_system

_start:
start:
        cli

        /* Clear BSS */
        xorl %eax, %eax
        movl $_sbss, %edi
        movl $_ebss, %ecx
        subl %edi, %ecx
        rep stosb

        /* Build identity-mapped page tables for first 4GB using 2MB pages.
         * PML4[0] -> PDPT
         * PDPT[0..3] -> PD[0..3]
         * PD[x][0..511] -> 2MB identity pages
         */

        /* Clear page table area */
        movl $rsw_pml4, %edi
        xorl %eax, %eax
        movl $(6*4096/4), %ecx  /* clear 6 pages */
        rep stosl

        /* PML4[0] -> PDPT */
        movl $rsw_pml4, %edi
        movl $rsw_pdpt, %eax
        orl $(PTE_P | PTE_RW), %eax
        movl %eax, (%edi)

        /* PDPT[0] -> PD0, PDPT[1] -> PD1, PDPT[2] -> PD2, PDPT[3] -> PD3 */
        movl $rsw_pdpt, %edi
        movl $rsw_pd0, %eax
        orl $(PTE_P | PTE_RW), %eax
        movl %eax, 0(%edi)
        movl $rsw_pd1, %eax
        orl $(PTE_P | PTE_RW), %eax
        movl %eax, 8(%edi)
        movl $rsw_pd2, %eax
        orl $(PTE_P | PTE_RW), %eax
        movl %eax, 16(%edi)
        movl $rsw_pd3, %eax
        orl $(PTE_P | PTE_RW), %eax
        movl %eax, 24(%edi)

        /* Fill PD0..PD3 with 2MB identity-mapped pages (total 4GB) */
        movl $rsw_pd0, %edi
        movl $(PTE_P | PTE_RW | PTE_PS), %eax  /* 0x0 + flags */
        movl $0, %edx                            /* high 32 bits */
        movl $(512*4), %ecx                      /* 512 entries * 4 PDs */
1:
        movl %eax, (%edi)
        movl %edx, 4(%edi)
        addl $0x200000, %eax                    /* next 2MB page */
        jnc 2f
        incl %edx                               /* carry into high bits */
2:
        addl $8, %edi
        decl %ecx
        jnz 1b

        /* Enable PAE */
        movl %cr4, %eax
        orl $CR4_PAE, %eax
        movl %eax, %cr4

        /* Load PML4 */
        movl $rsw_pml4, %eax
        movl %eax, %cr3

        /* Enable long mode via EFER MSR */
        movl $MSR_IA32_EFER, %ecx
        rdmsr
        orl $(EFER_LME | EFER_NXE), %eax
        wrmsr

        /* Enable paging to activate long mode */
        movl %cr0, %eax
        orl $(CR0_PG | CR0_PE | CR0_WP), %eax
        movl %eax, %cr0

        /* Load 64-bit GDT */
        lgdt rsw_gdt_desc

        /* Far jump to 64-bit code */
        pushl $0x08     /* code64 selector */
        pushl $start64
        lret

.code64
start64:
        /* Set up data segments */
        movl $0x10, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        xorl %eax, %eax
        movw %ax, %fs
        movw %ax, %gs

        /* Set up 64-bit stack */
        leaq _estack(%rip), %rsp

        /* Call RSW main */
        call rsw_main

_halt_system:
        cli
        hlt
        jmp _halt_system

/*
 * launch_hypervisor_32(entry_point)
 * Transitions from 64-bit long mode back to 32-bit protected mode,
 * then jumps to the hypervisor entry point.
 * rdi = entry point address (System V AMD64 ABI)
 */
.global launch_hypervisor_32
launch_hypervisor_32:
        cli
        /* Save entry point address for 32-bit code to use */
        movl %edi, compat_entry_addr(%rip)

        /* Load compat GDT (has 32-bit code segment) */
        lgdt compat_gdt_desc(%rip)

        /* Set up compat data segments */
        movl $0x18, %eax        /* compat data selector */
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        xorl %eax, %eax
        movw %ax, %fs
        movw %ax, %gs

        /* Far jump to compat 32-bit code segment.
         * We use lretq: push 32-bit CS selector and target address. */
        pushq $0x10             /* compat code32 selector */
        leaq to_compat(%rip), %rax
        pushq %rax
        lretq

.code32
to_compat:
        /* Now in compatibility mode (32-bit code in long mode) */
        /* Set up 32-bit data segments */
        movl $0x18, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss

        /* Disable paging to exit long mode */
        movl %cr0, %eax
        andl $0x7FFFFFFF, %eax  /* clear CR0.PG */
        movl %eax, %cr0

        /* Flush TLB */
        xorl %eax, %eax
        movl %eax, %cr3

        /* Disable long mode (clear EFER.LME) */
        movl $0xC0000080, %ecx  /* MSR_IA32_EFER */
        rdmsr
        andl $0xFFFFFEFF, %eax  /* clear LME (bit 8) */
        wrmsr

        /* Disable PAE */
        movl %cr4, %eax
        andl $0xFFFFFFDF, %eax  /* clear CR4.PAE */
        movl %eax, %cr4

        /* Load pure 32-bit GDT */
        lgdt pm32_gdt_desc

        /* Far jump to reload CS with 32-bit code segment */
        pushl $0x08
        pushl $in_pm32
        lret

in_pm32:
        /* Now in pure 32-bit protected mode */
        movl $0x10, %eax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %ss
        xorl %eax, %eax
        movw %ax, %fs
        movw %ax, %gs

        /* Jump to hypervisor entry point */
        jmp *compat_entry_addr

/* 64-bit GDT for the RSW */
.align 16
rsw_gdt:
        .quad 0x0000000000000000   /* 0x00: null */
        .quad 0x00AF9A000000FFFF   /* 0x08: code64 (L=1, D=0) */
        .quad 0x00CF92000000FFFF   /* 0x10: data64 */
rsw_gdt_end:

.align 4
rsw_gdt_desc:
        .word rsw_gdt_end - rsw_gdt - 1
        .long rsw_gdt

/* Compat GDT - adds a 32-bit code segment for the long-to-protected transition */
.align 16
compat_gdt:
        .quad 0x0000000000000000   /* 0x00: null */
        .quad 0x00AF9A000000FFFF   /* 0x08: code64 (L=1, D=0) */
        .quad 0x00CF9A000000FFFF   /* 0x10: code32 (L=0, D=1, present, DPL=0, code, readable) */
        .quad 0x00CF92000000FFFF   /* 0x18: data32/64 */
compat_gdt_end:

compat_gdt_desc:
        .word compat_gdt_end - compat_gdt - 1
        .quad compat_gdt

/* 32-bit only GDT for after long mode is disabled */
.align 16
pm32_gdt:
        .quad 0x0000000000000000   /* 0x00: null */
        .quad 0x00CF9A000000FFFF   /* 0x08: code32 */
        .quad 0x00CF92000000FFFF   /* 0x10: data32 */
pm32_gdt_end:

.align 4
pm32_gdt_desc:
        .word pm32_gdt_end - pm32_gdt - 1
        .long pm32_gdt

/* Storage for the hypervisor entry point address (written from 64-bit, read from 32-bit) */
.data
.align 4
.global compat_entry_addr
compat_entry_addr:
        .long 0

/* Page tables for identity mapping (4GB with 2MB pages) */
.bss
.align 4096
rsw_pml4:   .zero 4096
rsw_pdpt:   .zero 4096
rsw_pd0:    .zero 4096
rsw_pd1:    .zero 4096
rsw_pd2:    .zero 4096
rsw_pd3:    .zero 4096

.align 1024
.globl _stack
_stack:
	.zero (CONFIG_STACK_SIZE*1024)
.globl _estack
_estack:

.previous
