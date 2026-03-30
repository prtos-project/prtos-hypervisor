/*
 * start.S for x86-64 (native, bare-metal on QEMU with multiboot)
 *
 * Boot sequence: multiboot entry → set up long mode stack → BSS clear → GDT → IDT → PIC → main
 * QEMU -kernel with multiboot puts us in 32-bit protected mode.
 * We need a 32-bit entry that transitions to 64-bit long mode.
 */

/* Multiboot header */
.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

/* PVH ELF Note - allows QEMU -kernel to load a 64-bit ELF */
.section .note.PVH, "a", @note
.align 4
.long 4          /* namesz = sizeof("Xen") including NUL */
.long 4          /* descsz = 4 bytes (32-bit entry point) */
.long 18         /* type = XEN_ELFNOTE_PHYS32_ENTRY */
.asciz "Xen"     /* name */
.align 4
.long _start     /* 32-bit entry point address */

/*
 * 32-bit entry from multiboot. Set up minimal paging for long mode, then jump.
 */
.section .text
.code32

.globl _start
_start:
	cli

	/* Set up a temporary 32-bit stack */
	movl $(_stack + 0x4000), %esp

	/* Clear BSS in 32-bit mode (before paging, so page tables in BSS become zero) */
	movl $__bss_start, %edi
	movl $__bss_end, %ecx
	subl %edi, %ecx
	shrl $2, %ecx
	xorl %eax, %eax
	rep stosl

	/* Initialize page tables for identity mapping first 4GB
	   PML4[0] -> PDPT, PDPT[0..3] -> 1GB pages */
	/* PML4[0] = &boot_pdpt | PRESENT | WRITABLE */
	movl $boot_pdpt, %eax
	orl  $0x03, %eax
	movl %eax, boot_pml4

	/* PDPT[0] = 0GB, 1GB page, PRESENT|WRITABLE|PS */
	movl $0x00000083, boot_pdpt+ 0    /* 0x00000000 - 0x3FFFFFFF */
	movl $0x00000000, boot_pdpt+ 4
	/* PDPT[1] = 1GB page */
	movl $0x40000083, boot_pdpt+ 8    /* 0x40000000 - 0x7FFFFFFF */
	movl $0x00000000, boot_pdpt+12
	/* PDPT[2] = 2GB page */
	movl $0x80000083, boot_pdpt+16    /* 0x80000000 - 0xBFFFFFFF */
	movl $0x00000000, boot_pdpt+20
	/* PDPT[3] = 3GB page */
	movl $0xC0000083, boot_pdpt+24    /* 0xC0000000 - 0xFFFFFFFF */
	movl $0x00000000, boot_pdpt+28

	/* Set CR3 = PML4 */
	movl $boot_pml4, %eax
	movl %eax, %cr3

	/* Enable PAE (CR4.PAE, bit 5) */
	movl %cr4, %eax
	orl  $(1 << 5), %eax
	movl %eax, %cr4

	/* Enable long mode (EFER.LME, bit 8) */
	movl $0xC0000080, %ecx
	rdmsr
	orl  $(1 << 8), %eax
	wrmsr

	/* Enable paging (CR0.PG, bit 31) and write protect (CR0.WP, bit 16) */
	movl %cr0, %eax
	orl  $(1 << 31), %eax
	movl %eax, %cr0

	/* Load 64-bit GDT and far jump to 64-bit code */
	lgdt gdt32_ptr
	ljmp $0x08, $_start64

/*
 * 64-bit entry
 */
.code64
_start64:
	/* Set up data segments */
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss

	/* Set up 64-bit stack */
	movabsq $stack_top, %rsp

	/* BSS already cleared in 32-bit mode */

	/* Initialize PIC (remap IRQs to vectors 32-47) */
	call pic_init

	/* Load IDT */
	call idt_init

	/* Jump to main */
	call main

	/* Should not return */
1:	cli
	hlt
	jmp 1b

/*
 * PIC initialization: remap IRQ 0-7 to vectors 32-39, IRQ 8-15 to vectors 40-47
 */
pic_init:
	/* Start initialization sequence (ICW1) */
	movb $0x11, %al
	outb %al, $0x20        /* PIC1 command */
	outb %al, $0xA0        /* PIC2 command */

	/* ICW2: vector offsets */
	movb $32, %al
	outb %al, $0x21        /* PIC1 data: IRQ 0-7 → vectors 32-39 */
	movb $40, %al
	outb %al, $0xA1        /* PIC2 data: IRQ 8-15 → vectors 40-47 */

	/* ICW3: cascade */
	movb $4, %al
	outb %al, $0x21        /* PIC1: slave on IRQ2 */
	movb $2, %al
	outb %al, $0xA1        /* PIC2: cascade identity */

	/* ICW4: 8086 mode */
	movb $0x01, %al
	outb %al, $0x21
	outb %al, $0xA1

	/* Mask all interrupts except IRQ 0 (PIT timer) */
	movb $0xFE, %al        /* PIC1: unmask IRQ 0 only */
	outb %al, $0x21
	movb $0xFF, %al        /* PIC2: mask all */
	outb %al, $0xA1

	ret

/*
 * IDT initialization: set up 256 entries.
 * Only timer (vector 32 = _timer_isr) and yield (vector 0x80 = _yield_isr) are used.
 */
idt_init:
	/* Clear the IDT */
	movq $idt_table, %rdi
	xorq %rax, %rax
	movq $(256 * 2), %rcx   /* 256 entries × 16 bytes = 256 × 2 qwords */
	rep stosq

	/* Install timer ISR at vector 32 */
	movq $_timer_isr, %rsi
	movq $32, %rdi
	call idt_set_entry

	/* Install yield ISR at vector 0x80 */
	movq $_yield_isr, %rsi
	movq $0x80, %rdi
	call idt_set_entry

	/* Load the IDT */
	lidt idt_ptr
	ret

/*
 * idt_set_entry: set IDT[rdi] to handler at rsi
 *   rdi = vector number, rsi = handler address
 *   IDT entry is 16 bytes (interrupt gate, DPL=0, present)
 */
idt_set_entry:
	movq $idt_table, %rax
	shlq $4, %rdi           /* rdi = vector * 16 */
	addq %rdi, %rax         /* rax = &idt_table[vector] */

	/* Bytes 0-1: offset[15:0] */
	movw %si, (%rax)

	/* Bytes 2-3: code segment selector (0x08) */
	movw $0x08, 2(%rax)

	/* Byte 4: IST = 0 */
	movb $0x00, 4(%rax)

	/* Byte 5: type_attr = 0x8E (present, DPL=0, 64-bit interrupt gate) */
	movb $0x8E, 5(%rax)

	/* Bytes 6-7: offset[31:16] */
	movq %rsi, %rdx
	shrq $16, %rdx
	movw %dx, 6(%rax)

	/* Bytes 8-11: offset[63:32] */
	movq %rsi, %rdx
	shrq $32, %rdx
	movl %edx, 8(%rax)

	/* Bytes 12-15: reserved = 0 */
	movl $0, 12(%rax)

	ret

/*
 * Data
 */
.section .data

/* GDT for 64-bit mode */
.align 16
gdt64:
	.quad 0x0000000000000000    /* 0x00: null descriptor */
	.quad 0x00AF9A000000FFFF    /* 0x08: 64-bit code segment (DPL=0, L=1) */
	.quad 0x00CF92000000FFFF    /* 0x10: 64-bit data segment (DPL=0) */
gdt64_end:

.align 4
gdt32_ptr:
	.word gdt64_end - gdt64 - 1
	.long gdt64

.align 4
idt_ptr:
	.word (256 * 16) - 1       /* limit: 256 entries × 16 bytes */
	.quad idt_table

/*
 * BSS data
 */
.section .bss

/* IDT: 256 entries × 16 bytes = 4096 bytes */
.align 4096
idt_table:
	.space 256 * 16

/* Page tables for identity mapping */
.align 4096
boot_pml4:
	.space 4096
boot_pdpt:
	.space 4096

/* Stack: 32KB */
.align 16
.globl _stack
_stack:
	.space 0x8000
.globl stack_top
stack_top:
