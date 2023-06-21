/*
 * FILE: entry.S
 *
 * Interrupts' code
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
#define ROLLBACK_STACK movl $0, %ebp
#else
#define ROLLBACK_STACK
#endif

//To get the irq_cpu_ctxt of current current_kthread on the curent CPU
.macro GET_CPUCTXT _ctxt
        call get_local_processor
        mov _CURRENT_KTHREAD_OFFSET(,%eax, 1), %eax
        mov _IRQ_CPU_CTXT_OFFSET+_CTRL_OFFSET(%eax), \_ctxt\()
 .endm

.macro SET_CPUCTXT _ctxt
        call get_local_processor
        mov _CURRENT_KTHREAD_OFFSET(,%eax, 1), %eax
        mov \_ctxt\(), _IRQ_CPU_CTXT_OFFSET+_CTRL_OFFSET(%eax)
.endm
        
.macro SET_WP
        movl %cr0, %eax
        orl $(_CR0_WP), %eax
        movl %eax, %cr0
.endm
	
.macro CLEAR_WP
        movl %cr0, %eax
        andl $(~(_CR0_WP)), %eax
        movl %eax, %cr0
.endm

.macro HW_SAVE_REGS
        pushl %gs
        pushl %fs
        pushl %es
        pushl %ds
        pushl %eax
        pushl %ebp
        pushl %edi
        pushl %esi
        pushl %edx
        pushl %ecx
        pushl %ebx
.endm

.macro HW_RESTORE_REGS
        popl %ebx
        popl %ecx
        popl %edx
        popl %esi
        popl %edi
        popl %ebp
        popl %eax
1:      popl %ds
2:      popl %es
3:      popl %fs
4:      popl %gs
.section .gp_ex_tab, "a"
        ASM_ALIGN
        .long 1b
        .long 2b
        .long 3b
        .long 4b
.previous
.endm

.macro HW_SAVE_ALL
        cld
        HW_SAVE_REGS
        movl $(DS_SEL), %edx
        movl %edx, %ds
        movl %edx, %es
        xorl %edx, %edx
        movl %edx, %fs
        movl $(PERCPU_SEL), %edx
        movl %edx,%gs
.endm
	
.macro HW_RESTORE_ALL
        HW_RESTORE_REGS
        addl $8, %esp
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
.long 0
.previous
.endm
	
.macro BUILD_IRQ irq
.section .rodata.irq_handle,"a"
ASM_ALIGN
.long 1f
.text
ASM_ALIGN
1:
        pushl $0
        pushl $\irq\()
        jmp common_hyp_irq_body
.endm

.macro BUILD_TRAP_ERRCODE trap
.section .rodata.trap_handle,"a"
ASM_ALIGN
.long 1f
.text
ASM_ALIGN
1:
        pushl $\trap\()
        cli
        jmp common_hyp_trap_body
.endm
	
.macro BUILD_TRAP_NOERRCODE trap
.section .rodata.trap_handle,"a"
ASM_ALIGN
.long 1f
.text
ASM_ALIGN
1:
        pushl $0
        pushl $\trap\()
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
        movl \offset\()(%esp), %eax
        andl $0x3, %eax
        testl %eax, %eax
        jz \label\()
.endm

// define a macro function with parameter \p hndl
.macro COMMON_BODY hndlr
.text
ASM_ALIGN
common_hyp_\hndlr\()_body:
        sub $4, %esp
        HW_SAVE_ALL
        ROLLBACK_STACK
        
        GET_CPUCTXT %eax
        movl %eax, _PREV_OFFSET(%esp)
        SET_CPUCTXT %esp
        
        CHECK_USER_CTXT 1f, _CS_OFFSET
        CLEAR_WP
1:      pushl %esp
        call do_hyp_\hndlr\()
        addl $4, %esp
        CHECK_USER_CTXT 1f, _CS_OFFSET
        pushl %esp
        call raise_pend_irqs
        addl $4, %esp
        SET_WP
1:
        movl _PREV_OFFSET(%esp), %ecx
        SET_CPUCTXT %ecx
        HW_RESTORE_ALL
        add $4, %esp
        iret
.endm
	
// Define macro function common_hyp_irq_body
COMMON_BODY irq
// Define macro function common_hyp_trap_body
COMMON_BODY trap


.text
ASM_ALIGN
ENTRY(asm_hypercall_dispatch)
        pushf
        cli
        sub $8, %esp
        HW_SAVE_ALL
        ROLLBACK_STACK
        CLEAR_WP

        movl _ERR_CODE_OFFSET(%esp), %eax
        movl %eax, _FLAGS_OFFSET(%esp)

        GET_CPUCTXT %eax
        movl %eax, _PREV_OFFSET(%esp)
        SET_CPUCTXT %esp

        movl _AX_OFFSET(%esp), %eax
        cmpl $NR_HYPERCALLS, %eax
        jae 1f
        movl hypercalls_table(, %eax, 4), %eax
        testl %eax, %eax
        jz 1f
        call *%eax

        movl _AX_OFFSET(%esp), %ecx
        movl hypercall_flags_table(, %ecx, 4), %ecx
        test %ecx,%ecx
        jns 2f

        movl %eax, _AX_OFFSET(%esp)
        jmp 2f
1:      pushl _AX_OFFSET(%esp)
        pushl $unknown_hypercall_str
        call kprintf
        addl $0x8, %esp
        movl $-1, _AX_OFFSET(%esp)

2:
        pushl %esp
        call raise_pend_irqs
        addl $4, %esp

        movl _PREV_OFFSET(%esp), %ecx
        SET_CPUCTXT %ecx
        SET_WP
        HW_RESTORE_ALL
        add $4, %esp
        iret

ASM_ALIGN
ENTRY(asm_iret_handle)
        pushf
        cli
        sub $8, %esp
        HW_SAVE_ALL
        ROLLBACK_STACK
        CLEAR_WP

        movl _ERR_CODE_OFFSET(%esp), %eax
        movl %eax, _FLAGS_OFFSET(%esp)

        GET_CPUCTXT %eax
        movl %eax, _PREV_OFFSET(%esp)
        SET_CPUCTXT %esp
 
        call x86_iret_sys

        pushl %esp
        call raise_pend_irqs
        addl $4, %esp

        movl _PREV_OFFSET(%esp), %ecx
        SET_CPUCTXT %ecx
        SET_WP
        HW_RESTORE_ALL
        add $4, %esp
        iret
        
ASM_ALIGN
ENTRY(unexpected_irq)
        pushl $0x0
        pushl $0xff
        HW_SAVE_ALL
        ROLLBACK_STACK
        CHECK_USER_CTXT 1f, _CS_OFFSET
        CLEAR_WP
1:      pushl $unexpec_irq_str
        call kprintf
        addl $4, %esp
        CHECK_USER_CTXT 1f, _CS_OFFSET
        SET_WP
1:      HW_RESTORE_ALL
        iret

.data
	
unexpec_irq_str:
    .asciz "Unexpected irq\n"
	
unknown_hypercall_str:
    .asciz "Hypercall (%d) unknown\n"

unknown_ipi_str:
    .asciz "IPI (%d) unkown\n"
