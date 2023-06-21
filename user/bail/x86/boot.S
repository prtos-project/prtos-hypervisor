/*
 * FILE: boot.S
 *
 * Generic boot code, sets up a valid stack and jumps to the C code
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
.align 4

.global start, _start

_start:
start:
    movl $_end_early_stack, %esp  /* shared stack, works because all */
    movl $get_vcpuid_nr, %eax /* vCPUs stack identical values on lcall */
    __PRTOS_HC
    cmpl $MAIN_VCPU, %eax
    jne 2f

    xorl %eax, %eax
    movl $_sbss, %edi
    movl $_ebss, %ecx
    subl %edi, %ecx
    rep
    stosb

    xorl %eax, %eax
2:
    movl $STACK_SIZE, %ecx
    mull %ecx
    addl $STACK_SIZE, %eax
    addl $_stack, %eax
    movl %eax, %esp

    pushl %ebx
    call init_libprtos
    call init_arch
    call setup_irqs
    call partition_main

    call part_halt

1:
    jmp 1b

#define HW_SAVE_ALL \
    cld ; \
    pushl %gs ; \
    pushl %fs ; \
    pushl %es ; \
    pushl %ds ; \
    pushl %eax ; \
    pushl %ebp ; \
    pushl %edi ; \
    pushl %esi ; \
    pushl %edx ; \
    pushl %ecx ; \
    pushl %ebx	
	
#define HW_RESTORE_ALL  \
    popl %ebx ; \
    popl %ecx ; \
    popl %edx ; \
    popl %esi ; \
    popl %edi ; \
    popl %ebp ; \
    popl %eax ; \
    popl %ds ; \
    popl %es ; \
    popl %fs ; \
    popl %gs ; \
    addl $8, %esp
	
common_part_trap_body:
    HW_SAVE_ALL
    mov 44(%esp), %eax // Get part interruption vector number pre-strored in part stack in BUILD_TRAP_NOERRCODE or BUILD_TRAP_ERRCODE Macro function
    movl part_trap_handlers_table(, %eax, 4), %eax
    pushl %esp
    call *%eax
    addl $4, %esp
    HW_RESTORE_ALL
    jmp prtos_x86_iret

.macro TABLE_START symbol
.section .data
.globl \symbol\()
ASM_ALIGN
\symbol\() :
.endm

.macro TABLE_END
.section .data
ASM_ALIGN
.long 0
.previous
.endm

.macro BUILD_TRAP_ERRCODE trap
.section .data
ASM_ALIGN
.long 1f
.text
ASM_ALIGN
1:
       	pushl $\trap\()
       	jmp common_part_trap_body
.endm
	
.macro BUILD_TRAP_NOERRCODE trap
.section .data
ASM_ALIGN
.long 1f
.text
ASM_ALIGN
1:
       	pushl $0
       	pushl $\trap\()
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
	.word NUM_OF_PART_IDT_ENTRIES*8-1
	.long part_idt_table
	
part_gdt_table:
        .quad 0x0000000000000000 /* NULL  descriptor */
        .quad 0x00cfba000000bfff /* 1<<3 code segment R1 */
        .quad 0x00cfb2000000bfff /* 2<<3 data segment R1 */

.word 0
ENTRY(part_gdt_desc)
        .word 3*8-1
        .long part_gdt_table

debug_string:
	.asciz "BAIL Irq: Calling address 0x%08x\n"
	
.bss

.globl _stack
_stack:
        .fill (STACK_SIZE/4)*CONFIG_MAX_NO_VCPUS,4,0

.globl _start_early_stack
_start_early_stack:
        .zero 128
.globl _end_early_stack
_end_early_stack:

.globl part_idt_table
part_idt_table:
	.zero (NUM_OF_PART_IDT_ENTRIES*8)

.previous

