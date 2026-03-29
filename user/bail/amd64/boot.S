/*
 * FILE: boot.S
 *
 * Generic boot code for amd64, sets up a valid stack and jumps to C code
 *
 * www.prtos.org
 */
#include <prtos.h>
#include <prtos_inc/arch/asm_offsets.h>

#define MIN_STACK_FRAME 0x60
#define NO_PGTS 8
#define NUM_OF_PART_IDT_ENTRIES (256+32)
#define MAIN_VCPU 0x0

.text
.align 8

.global start, _start

_start:
start:
    leaq _end_early_stack(%rip), %rsp
    movq $get_vcpuid_nr, %rax
    __PRTOS_HC
    cmpq $MAIN_VCPU, %rax
    jne 2f

    xorq %rax, %rax
    leaq _sbss(%rip), %rdi
    leaq _ebss(%rip), %rcx
    subq %rdi, %rcx
    rep
    stosb

    xorq %rax, %rax
2:
    movq $STACK_SIZE, %rcx
    mulq %rcx
    addq $STACK_SIZE, %rax
    leaq _stack(%rip), %rcx
    addq %rcx, %rax
    movq %rax, %rsp

    movq %rbx, %rdi
    call init_libprtos
    call init_arch
    call setup_irqs
    call partition_main

    call part_halt

1:
    jmp 1b

#define HW_SAVE_ALL \
    cld ; \
    pushq %rax ; \
    pushq %rbp ; \
    pushq %rdi ; \
    pushq %rsi ; \
    pushq %rdx ; \
    pushq %rcx ; \
    pushq %rbx ; \
    pushq %r8  ; \
    pushq %r9  ; \
    pushq %r10 ; \
    pushq %r11 ; \
    pushq %r12 ; \
    pushq %r13 ; \
    pushq %r14 ; \
    pushq %r15

#define HW_RESTORE_ALL  \
    popq %r15 ; \
    popq %r14 ; \
    popq %r13 ; \
    popq %r12 ; \
    popq %r11 ; \
    popq %r10 ; \
    popq %r9  ; \
    popq %r8  ; \
    popq %rbx ; \
    popq %rcx ; \
    popq %rdx ; \
    popq %rsi ; \
    popq %rdi ; \
    popq %rbp ; \
    popq %rax ; \
    addq $16, %rsp

common_part_trap_body:
    HW_SAVE_ALL
    /* Get trap vector number from the stack (pushed before regs) */
    movq 120(%rsp), %rax  /* 15 regs * 8 bytes = 120, then the vector is there */
    leaq part_trap_handlers_table(%rip), %rcx
    movq (%rcx, %rax, 8), %rax
    movq %rsp, %rdi
    call *%rax
    HW_RESTORE_ALL
    jmp prtos_x86_iret

.macro TABLE_START symbol
.section .data
.globl \symbol\()
.align 8
\symbol\() :
.endm

.macro TABLE_END
.section .data
.align 8
.quad 0
.previous
.endm

.macro BUILD_TRAP_ERRCODE trap
.section .data
.align 8
.quad 1f
.text
.align 8
1:
        pushq $\trap\()
        jmp common_part_trap_body
.endm

.macro BUILD_TRAP_NOERRCODE trap
.section .data
.align 8
.quad 1f
.text
.align 8
1:
        pushq $0
        pushq $\trap\()
        jmp common_part_trap_body
.endm

.macro BUILD_TRAP_BLOCK start_trap_number end_trap_number
vector=\start_trap_number\()
.rept \end_trap_number\()-\start_trap_number\()
	BUILD_TRAP_NOERRCODE vector
vector=vector+1
.endr
.endm

// Traps routines
TABLE_START vtrap_table
BUILD_TRAP_NOERRCODE 0x0
BUILD_TRAP_NOERRCODE 0x1
BUILD_TRAP_NOERRCODE 0x2
BUILD_TRAP_NOERRCODE 0x3
BUILD_TRAP_NOERRCODE 0x4
BUILD_TRAP_NOERRCODE 0x5
BUILD_TRAP_NOERRCODE 0x6
BUILD_TRAP_NOERRCODE 0x7
BUILD_TRAP_ERRCODE 0x8
BUILD_TRAP_NOERRCODE 0x9
BUILD_TRAP_ERRCODE 0xa
BUILD_TRAP_ERRCODE 0xb
BUILD_TRAP_ERRCODE 0xc
BUILD_TRAP_ERRCODE 0xd
BUILD_TRAP_ERRCODE 0xe
BUILD_TRAP_NOERRCODE 0xf
BUILD_TRAP_NOERRCODE 0x10
BUILD_TRAP_ERRCODE 0x11
BUILD_TRAP_NOERRCODE 0x12
BUILD_TRAP_NOERRCODE 0x13
BUILD_TRAP_ERRCODE 0x14
BUILD_TRAP_ERRCODE 0x15
BUILD_TRAP_ERRCODE 0x16
BUILD_TRAP_ERRCODE 0x17
BUILD_TRAP_ERRCODE 0x18
BUILD_TRAP_ERRCODE 0x19
BUILD_TRAP_ERRCODE 0x1a
BUILD_TRAP_ERRCODE 0x1b
BUILD_TRAP_ERRCODE 0x1c
BUILD_TRAP_ERRCODE 0x1d
BUILD_TRAP_ERRCODE 0x1e
BUILD_TRAP_ERRCODE 0x1f
BUILD_TRAP_BLOCK 0x20 NUM_OF_PART_IDT_ENTRIES
TABLE_END

.data
.word 0
ENTRY(part_idt_desc)
	.word NUM_OF_PART_IDT_ENTRIES*16-1
	.quad part_idt_table

part_gdt_table:
        .quad 0x0000000000000000 /* NULL descriptor */
        .quad 0x00afba000000bfff /* 1<<3 code segment L=1 D=0 DPL=1 limit<PRTOS_OFFSET */
        .quad 0x00cfb2000000bfff /* 2<<3 data segment DPL=1 limit<PRTOS_OFFSET */

.word 0
ENTRY(part_gdt_desc)
        .word 3*8-1
        .quad part_gdt_table

debug_string:
	.asciz "BAIL Irq: Calling address 0x%016lx\n"

.bss

.globl _stack
_stack:
        .fill (STACK_SIZE/8)*CONFIG_MAX_NO_VCPUS,8,0

.globl _start_early_stack
_start_early_stack:
        .zero 256
.globl _end_early_stack
_end_early_stack:

.globl part_idt_table
part_idt_table:
	.zero (NUM_OF_PART_IDT_ENTRIES*16)

.previous
