/*
 * portASM.S for x86-64 (native, bare-metal)
 *
 * Context save/restore and interrupt handling for FreeRTOS on x86-64.
 *
 * Stack frame layout (20 quadwords = 160 bytes):
 *   offset 0:   ullPortTaskHasFPUContext
 *   offset 8:   ullCriticalNesting
 *   offset 16:  rflags
 *   offset 24:  rip (return address)
 *   offset 32:  rax
 *   offset 40:  rcx
 *   offset 48:  rdx
 *   offset 56:  rbx
 *   offset 64:  rbp
 *   offset 72:  rsi
 *   offset 80:  rdi
 *   offset 88:  r8
 *   offset 96:  r9
 *   offset 104: r10
 *   offset 112: r11
 *   offset 120: r12
 *   offset 128: r13
 *   offset 136: r14
 *   offset 144: r15
 */

#define portCONTEXT_SIZE  152

.macro portSAVE_CONTEXT
	/* Allocate stack frame */
	subq $portCONTEXT_SIZE, %rsp

	/* Save general purpose registers */
	movq %r15, 144(%rsp)
	movq %r14, 136(%rsp)
	movq %r13, 128(%rsp)
	movq %r12, 120(%rsp)
	movq %r11, 112(%rsp)
	movq %r10, 104(%rsp)
	movq %r9,  96(%rsp)
	movq %r8,  88(%rsp)
	movq %rdi, 80(%rsp)
	movq %rsi, 72(%rsp)
	movq %rbp, 64(%rsp)
	movq %rbx, 56(%rsp)
	movq %rdx, 48(%rsp)
	movq %rcx, 40(%rsp)
	movq %rax, 32(%rsp)

	/* rip is already saved by the interrupt/call mechanism */
	/* rflags: push and pop to save */
	pushfq
	popq %rax
	movq %rax, 16(%rsp)

	/* Save critical nesting count */
	movq ullCriticalNesting(%rip), %rax
	movq %rax, 8(%rsp)

	/* Save FPU context flag */
	movq ullPortTaskHasFPUContext(%rip), %rax
	movq %rax, 0(%rsp)

	/* Save the stack pointer into the TCB */
	movq pxCurrentTCB(%rip), %rax
	movq %rsp, (%rax)
.endm

.macro portRESTORE_CONTEXT
	/* Load the stack pointer from the TCB */
	movq pxCurrentTCB(%rip), %rax
	movq (%rax), %rsp

	/* Restore FPU context flag */
	movq 0(%rsp), %rax
	movq %rax, ullPortTaskHasFPUContext(%rip)

	/* Restore critical nesting count */
	movq 8(%rsp), %rax
	movq %rax, ullCriticalNesting(%rip)

	/* Write RFLAGS from context copy back to interrupt frame */
	movq 16(%rsp), %rax
	movq %rax, (portCONTEXT_SIZE+16)(%rsp)

	/* Write RIP from context copy back to interrupt frame */
	movq 24(%rsp), %rax
	movq %rax, portCONTEXT_SIZE(%rsp)

	/* Restore general purpose registers */
	movq 32(%rsp), %rax
	movq 40(%rsp), %rcx
	movq 48(%rsp), %rdx
	movq 56(%rsp), %rbx
	movq 64(%rsp), %rbp
	movq 72(%rsp), %rsi
	movq 80(%rsp), %rdi
	movq 88(%rsp), %r8
	movq 96(%rsp), %r9
	movq 104(%rsp), %r10
	movq 112(%rsp), %r11
	movq 120(%rsp), %r12
	movq 128(%rsp), %r13
	movq 136(%rsp), %r14
	movq 144(%rsp), %r15

	/* Skip context frame, RSP now points to interrupt frame */
	addq $portCONTEXT_SIZE, %rsp

	/* Return via iretq: pops RIP, CS, RFLAGS, RSP, SS */
	iretq
.endm

/*-----------------------------------------------------------*/

	.align 16
	.globl vPortRestoreTaskContext
vPortRestoreTaskContext:
	/* Load the stack pointer from the TCB */
	movq pxCurrentTCB(%rip), %rax
	movq (%rax), %rsp

	/* Restore FPU context flag */
	movq 0(%rsp), %rax
	movq %rax, ullPortTaskHasFPUContext(%rip)

	/* Restore critical nesting count */
	movq 8(%rsp), %rax
	movq %rax, ullCriticalNesting(%rip)

	/* Write RFLAGS from context copy back to interrupt frame */
	movq 16(%rsp), %rax
	movq %rax, (portCONTEXT_SIZE+16)(%rsp)

	/* Write RIP from context copy back to interrupt frame */
	movq 24(%rsp), %rax
	movq %rax, portCONTEXT_SIZE(%rsp)

	/* Restore general purpose registers */
	movq 32(%rsp), %rax
	movq 40(%rsp), %rcx
	movq 48(%rsp), %rdx
	movq 56(%rsp), %rbx
	movq 64(%rsp), %rbp
	movq 72(%rsp), %rsi
	movq 80(%rsp), %rdi
	movq 88(%rsp), %r8
	movq 96(%rsp), %r9
	movq 104(%rsp), %r10
	movq 112(%rsp), %r11
	movq 120(%rsp), %r12
	movq 128(%rsp), %r13
	movq 136(%rsp), %r14
	movq 144(%rsp), %r15

	/* Skip context frame, RSP now points to interrupt frame */
	addq $portCONTEXT_SIZE, %rsp

	/* Return to task via iretq: pops RIP, CS, RFLAGS, RSP, SS */
	iretq

/*-----------------------------------------------------------*/

/*
 * Timer interrupt handler (IDT vector 32 = PIT IRQ 0)
 *
 * CPU pushes: SS, RSP, RFLAGS, CS, RIP onto the stack.
 * We save context, handle tick, then restore (possibly different) task.
 */
	.align 16
	.globl _timer_isr
_timer_isr:
	cli

	/* Save all registers into current task context */
	subq $portCONTEXT_SIZE, %rsp

	movq %r15, 144(%rsp)
	movq %r14, 136(%rsp)
	movq %r13, 128(%rsp)
	movq %r12, 120(%rsp)
	movq %r11, 112(%rsp)
	movq %r10, 104(%rsp)
	movq %r9,  96(%rsp)
	movq %r8,  88(%rsp)
	movq %rdi, 80(%rsp)
	movq %rsi, 72(%rsp)
	movq %rbp, 64(%rsp)
	movq %rbx, 56(%rsp)
	movq %rdx, 48(%rsp)
	movq %rcx, 40(%rsp)
	movq %rax, 32(%rsp)

	/* Save RIP from the interrupt frame (pushed by CPU at rsp+portCONTEXT_SIZE) */
	movq portCONTEXT_SIZE(%rsp), %rax
	movq %rax, 24(%rsp)

	/* Save RFLAGS from interrupt frame */
	movq (portCONTEXT_SIZE+16)(%rsp), %rax
	movq %rax, 16(%rsp)

	/* Save critical nesting count */
	movq ullCriticalNesting(%rip), %rax
	movq %rax, 8(%rsp)

	/* Save FPU context flag */
	movq ullPortTaskHasFPUContext(%rip), %rax
	movq %rax, 0(%rsp)

	/* Save stack to TCB */
	movq pxCurrentTCB(%rip), %rax
	movq %rsp, (%rax)

	/* Increment interrupt nesting */
	movq ullPortInterruptNesting(%rip), %rax
	incq %rax
	movq %rax, ullPortInterruptNesting(%rip)

	/* Call tick handler */
	call FreeRTOS_Tick_Handler

	/* Send EOI to PIC */
	movb $0x20, %al
	outb %al, $0x20

	/* Check if context switch required */
	movq ullPortYieldRequired(%rip), %rax
	testq %rax, %rax
	jz _timer_no_switch

	movq $0, ullPortYieldRequired(%rip)
	call vTaskSwitchContext

_timer_no_switch:
	/* Decrement interrupt nesting */
	movq ullPortInterruptNesting(%rip), %rax
	decq %rax
	movq %rax, ullPortInterruptNesting(%rip)

	/* Restore (possibly new) task context */
	movq pxCurrentTCB(%rip), %rax
	movq (%rax), %rsp

	/* Restore FPU context flag */
	movq 0(%rsp), %rax
	movq %rax, ullPortTaskHasFPUContext(%rip)

	/* Restore critical nesting */
	movq 8(%rsp), %rax
	movq %rax, ullCriticalNesting(%rip)

	/* Restore RFLAGS into interrupt frame */
	movq 16(%rsp), %rax
	movq %rax, (portCONTEXT_SIZE+16)(%rsp)

	/* Restore RIP into interrupt frame */
	movq 24(%rsp), %rax
	movq %rax, portCONTEXT_SIZE(%rsp)

	/* Restore GP registers */
	movq 32(%rsp), %rax
	movq 40(%rsp), %rcx
	movq 48(%rsp), %rdx
	movq 56(%rsp), %rbx
	movq 64(%rsp), %rbp
	movq 72(%rsp), %rsi
	movq 80(%rsp), %rdi
	movq 88(%rsp), %r8
	movq 96(%rsp), %r9
	movq 104(%rsp), %r10
	movq 112(%rsp), %r11
	movq 120(%rsp), %r12
	movq 128(%rsp), %r13
	movq 136(%rsp), %r14
	movq 144(%rsp), %r15

	addq $portCONTEXT_SIZE, %rsp

	iretq

/*-----------------------------------------------------------*/

/*
 * Yield handler (int $0x80)
 * Same as timer ISR but does vTaskSwitchContext unconditionally.
 */
	.align 16
	.globl _yield_isr
_yield_isr:
	cli

	subq $portCONTEXT_SIZE, %rsp

	movq %r15, 144(%rsp)
	movq %r14, 136(%rsp)
	movq %r13, 128(%rsp)
	movq %r12, 120(%rsp)
	movq %r11, 112(%rsp)
	movq %r10, 104(%rsp)
	movq %r9,  96(%rsp)
	movq %r8,  88(%rsp)
	movq %rdi, 80(%rsp)
	movq %rsi, 72(%rsp)
	movq %rbp, 64(%rsp)
	movq %rbx, 56(%rsp)
	movq %rdx, 48(%rsp)
	movq %rcx, 40(%rsp)
	movq %rax, 32(%rsp)

	movq portCONTEXT_SIZE(%rsp), %rax
	movq %rax, 24(%rsp)

	movq (portCONTEXT_SIZE+16)(%rsp), %rax
	movq %rax, 16(%rsp)

	movq ullCriticalNesting(%rip), %rax
	movq %rax, 8(%rsp)

	movq ullPortTaskHasFPUContext(%rip), %rax
	movq %rax, 0(%rsp)

	movq pxCurrentTCB(%rip), %rax
	movq %rsp, (%rax)

	movq ullPortInterruptNesting(%rip), %rax
	incq %rax
	movq %rax, ullPortInterruptNesting(%rip)

	call vTaskSwitchContext

	movq $0, ullPortYieldRequired(%rip)

	movq ullPortInterruptNesting(%rip), %rax
	decq %rax
	movq %rax, ullPortInterruptNesting(%rip)

	movq pxCurrentTCB(%rip), %rax
	movq (%rax), %rsp

	movq 0(%rsp), %rax
	movq %rax, ullPortTaskHasFPUContext(%rip)

	movq 8(%rsp), %rax
	movq %rax, ullCriticalNesting(%rip)

	movq 16(%rsp), %rax
	movq %rax, (portCONTEXT_SIZE+16)(%rsp)

	movq 24(%rsp), %rax
	movq %rax, portCONTEXT_SIZE(%rsp)

	movq 32(%rsp), %rax
	movq 40(%rsp), %rcx
	movq 48(%rsp), %rdx
	movq 56(%rsp), %rbx
	movq 64(%rsp), %rbp
	movq 72(%rsp), %rsi
	movq 80(%rsp), %rdi
	movq 88(%rsp), %r8
	movq 96(%rsp), %r9
	movq 104(%rsp), %r10
	movq 112(%rsp), %r11
	movq 120(%rsp), %r12
	movq 128(%rsp), %r13
	movq 136(%rsp), %r14
	movq 144(%rsp), %r15

	addq $portCONTEXT_SIZE, %rsp

	iretq
