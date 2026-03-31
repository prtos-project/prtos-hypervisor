/*
 * FILE: entry.S
 *
 * Interrupt/trap entry points for amd64
 *
 * www.prtos.org
 */

#include <linkage.h>
#include <hypercalls.h>
#include <ktimer.h>
#include <arch/asm_offsets.h>
#include <arch/irqs.h>
#include <arch/processor.h>
#include <arch/segments.h>
#include <arch/prtos_def.h>

#ifdef CONFIG_DEBUG
#define ROLLBACK_STACK xorq %rbp, %rbp
#else
#define ROLLBACK_STACK
#endif

.macro GET_CPUCTXT _ctxt
        call get_local_processor
        movq _CURRENT_KTHREAD_OFFSET(%rax), %rax
        movq _IRQ_CPU_CTXT_OFFSET+_CTRL_OFFSET(%rax), \_ctxt\()
.endm

.macro SET_CPUCTXT _ctxt
        call get_local_processor
        movq _CURRENT_KTHREAD_OFFSET(%rax), %rax
        movq \_ctxt\(), _IRQ_CPU_CTXT_OFFSET+_CTRL_OFFSET(%rax)
.endm

.macro SET_WP
        movq %cr0, %rax
        orq $(_CR0_WP), %rax
        movq %rax, %cr0
.endm

.macro CLEAR_WP
        movq %cr0, %rax
        andq $(~(_CR0_WP)), %rax
        movq %rax, %cr0
.endm

.macro HW_SAVE_REGS
        pushq %rax
        pushq %rbp
        pushq %rdi
        pushq %rsi
        pushq %rdx
        pushq %rcx
        pushq %rbx
        pushq %r8
        pushq %r9
        pushq %r10
        pushq %r11
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15
.endm

.macro HW_RESTORE_REGS
        popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %r11
        popq %r10
        popq %r9
        popq %r8
        popq %rbx
        popq %rcx
        popq %rdx
        popq %rsi
        popq %rdi
        popq %rbp
        popq %rax
.endm

.macro HW_SAVE_ALL
        cld
        HW_SAVE_REGS
        movl $(DS_SEL), %edx
        movw %dx, %ds
        movw %dx, %es
        xorl %edx, %edx
        movw %dx, %fs
        movl $(PERCPU_SEL), %edx
        movw %dx, %gs
.endm

.macro HW_RESTORE_ALL
        HW_RESTORE_REGS
        addq $16, %rsp  /* skip prev + irq_nr/err_code */
.endm

.macro TABLE_START section, symbol
.section .rodata.\section\(),"a"
.globl \symbol\()
ASM_ALIGN
\symbol\() :
.endm

.macro TABLE_END section
.section .rodata.\section\(),"a"
ASM_ALIGN
.quad 0
.previous
.endm

.macro BUILD_IRQ irq
.section .rodata.irq_handle,"a"
ASM_ALIGN
.quad 1f
.text
ASM_ALIGN
1:
        pushq $0
        pushq $\irq\()
        jmp common_hyp_irq_body
.endm

.macro BUILD_TRAP_ERRCODE trap
.section .rodata.trap_handle,"a"
ASM_ALIGN
.quad 1f
.text
ASM_ALIGN
1:
        pushq $\trap\()
        cli
        jmp common_hyp_trap_body
.endm

.macro BUILD_TRAP_NOERRCODE trap
.section .rodata.trap_handle,"a"
ASM_ALIGN
.quad 1f
.text
ASM_ALIGN
1:
        pushq $0
        pushq $\trap\()
        cli
        jmp common_hyp_trap_body
.endm

// HW irq routines
TABLE_START irq_handle, hyp_irq_handlers_table
vector=0
.rept CONFIG_NO_HWIRQS
        BUILD_IRQ vector
vector=vector+1
.endr
TABLE_END irq_handle

// Traps routines
TABLE_START trap_handle, hyp_trap_handlers_table
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
TABLE_END trap_handle

.macro CHECK_USER_CTXT label, offset
        movq \offset\()(%rsp), %rax
        andl $0x3, %eax
        testl %eax, %eax
        jz \label\()
.endm

.macro COMMON_BODY hndlr
.text
ASM_ALIGN
common_hyp_\hndlr\()_body:
        subq $8, %rsp   /* space for prev pointer */
        HW_SAVE_ALL
        ROLLBACK_STACK

        GET_CPUCTXT %rax
        movq %rax, _PREV_OFFSET(%rsp)
        SET_CPUCTXT %rsp

        CHECK_USER_CTXT 1f, _CS_OFFSET
        CLEAR_WP
1:      movq %rsp, %rdi
        call do_hyp_\hndlr\()

        CHECK_USER_CTXT 1f, _CS_OFFSET
        movq %rsp, %rdi
        call raise_pend_irqs
        SET_WP
1:
        movq _PREV_OFFSET(%rsp), %rcx
        SET_CPUCTXT %rcx
        HW_RESTORE_ALL
        addq $8, %rsp   /* skip prev */
        iretq
.endm

COMMON_BODY irq
COMMON_BODY trap

/* Hypercall entry via int $0x82 */
.text
ASM_ALIGN
ENTRY(asm_hypercall_dispatch)
        cli
        pushq $0     /* err_code placeholder */
        pushq $0     /* irq_nr placeholder */
        subq $8, %rsp  /* prev */
        HW_SAVE_ALL
        ROLLBACK_STACK
        CLEAR_WP

        /* Save flags from stack (pushed by int instruction) */
        movq _FLAGS_OFFSET(%rsp), %rax
        /* rax register has hypercall number */
        movq _AX_OFFSET(%rsp), %rax

        GET_CPUCTXT %r12
        movq %r12, _PREV_OFFSET(%rsp)
        SET_CPUCTXT %rsp

        movq _AX_OFFSET(%rsp), %rax
        cmpq $NR_HYPERCALLS, %rax
        jae 1f
        leaq hypercalls_table(%rip), %r12
        movq (%r12, %rax, 8), %r12
        testq %r12, %r12
        jz 1f

        /* System V ABI: args in rdi, rsi, rdx, rcx, r8, r9
         * But our hypercall args come from saved regs on stack:
         * arg0=rbx, arg1=rcx, arg2=rdx, arg3=rsi, arg4=rdi */
        movq _BX_OFFSET(%rsp), %rdi
        movq _CX_OFFSET(%rsp), %rsi
        movq _DX_OFFSET(%rsp), %rdx
        movq _SI_OFFSET(%rsp), %rcx
        movq _DI_OFFSET(%rsp), %r8
        call *%r12

        movq _AX_OFFSET(%rsp), %rcx
        leaq hypercall_flags_table(%rip), %r12
        movq (%r12, %rcx, 8), %rcx
        testq %rcx, %rcx
        jns 2f

        movq %rax, _AX_OFFSET(%rsp)
        jmp 2f

1:      movq _AX_OFFSET(%rsp), %rdi
        leaq unknown_hypercall_str(%rip), %rdi
        call kprintf
        movq $-1, _AX_OFFSET(%rsp)

2:
        movq %rsp, %rdi
        call raise_pend_irqs

        movq _PREV_OFFSET(%rsp), %rcx
        SET_CPUCTXT %rcx
        SET_WP
        HW_RESTORE_ALL
        addq $8, %rsp
        iretq

/* IRET handler via int $0x83 */
ASM_ALIGN
ENTRY(asm_iret_handle)
        cli
        pushq $0
        pushq $0
        subq $8, %rsp
        HW_SAVE_ALL
        ROLLBACK_STACK
        CLEAR_WP

        GET_CPUCTXT %rax
        movq %rax, _PREV_OFFSET(%rsp)
        SET_CPUCTXT %rsp

        call amd64_iret_sys

        movq %rsp, %rdi
        call raise_pend_irqs

        movq _PREV_OFFSET(%rsp), %rcx
        SET_CPUCTXT %rcx
        SET_WP
        HW_RESTORE_ALL
        addq $8, %rsp
        iretq

ASM_ALIGN
ENTRY(unexpected_irq)
        pushq $0x0
        pushq $0xff
        HW_SAVE_ALL
        ROLLBACK_STACK
        CHECK_USER_CTXT 1f, _CS_OFFSET
        CLEAR_WP
1:      leaq unexpec_irq_str(%rip), %rdi
        call kprintf
        CHECK_USER_CTXT 1f, _CS_OFFSET
        SET_WP
1:      HW_RESTORE_ALL
        iretq

.data

unexpec_irq_str:
    .asciz "Unexpected irq\n"

unknown_hypercall_str:
    .asciz "Hypercall (%d) unknown\n"

unknown_ipi_str:
    .asciz "IPI (%d) unknown\n"
