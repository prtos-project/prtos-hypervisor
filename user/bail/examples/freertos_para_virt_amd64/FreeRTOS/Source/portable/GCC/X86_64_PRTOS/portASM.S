/*
 * portASM.S for x86-64 para-virt (PRTOS hypervisor)
 *
 * Same context layout as native port but uses:
 * - prtos_x86_clear_if() instead of cli
 * - int $0x83 (prtos_x86_iret) instead of iretq
 * - No PIC EOI (PRTOS handles interrupt controller)
 *
 * PRTOS delivers virtual interrupts by pushing a real interrupt frame
 * (SS, RSP, RFLAGS, CS, RIP) onto the partition's stack, same as hardware.
 */

#define portCONTEXT_SIZE  152

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

	/* Return to task via PRTOS virtual iret: pops RIP, CS, RFLAGS, RSP, SS */
	int $0x83

/*-----------------------------------------------------------*/

/*
 * Timer interrupt handler (PRTOS ext HW timer, IDT vector 224)
 *
 * PRTOS pushes: SS, RSP, RFLAGS, CS, RIP onto the partition stack.
 * We save context, handle tick, then restore (possibly different) task.
 *
 * IMPORTANT: Save all GP registers FIRST before calling any C functions,
 * because C calling convention clobbers rax,rcx,rdx,rsi,rdi,r8-r11.
 */
#define AMD64_CLEAR_IF_NR 43

	.align 16
	.globl _timer_isr
_timer_isr:
	/* Save all GP registers FIRST (before any function calls) */
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

	/* Now disable virtual interrupts via inline hypercall.
	 * int $0x82 with rax=hypercall_nr. Only clobbers rax. */
	movq $AMD64_CLEAR_IF_NR, %rax
	int $0x82
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

	/* No PIC EOI needed - PRTOS handles interrupt controller */

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

	/* PRTOS virtual iret */
	int $0x83

/*-----------------------------------------------------------*/

/*
 * vPortYieldCall - Direct function call yield for PRTOS para-virt.
 *
 * Called via normal CALL instruction (not software interrupt).
 * Manually builds an interrupt frame, saves context, switches tasks,
 * and returns to the new task via int $0x83.
 *
 * Under PRTOS amd64, partitions run at ring 3 but the hypervisor IDT
 * only allows int $0x82 and $0x83 from ring 3. So we can't use a
 * software interrupt for yield; instead we build the frame manually.
 */
#define GUEST_CS_SEL  0x13b
#define GUEST_DS_SEL  0x143

	.align 16
	.globl vPortYieldCall
vPortYieldCall:
	call prtos_x86_clear_if

	/* Pop return address (pushed by the CALL to vPortYieldCall) */
	popq %r11		/* r11 = return RIP (caller-saved, safe to clobber) */

	/* RSP is now at caller's original RSP.
	 * Build a fake interrupt frame on the stack. */
	pushq $GUEST_DS_SEL	/* SS */
	leaq 8(%rsp), %rax	/* Calculate caller's original RSP */
	pushq %rax		/* RSP (caller's original) */
	pushfq			/* RFLAGS (real IF=1 always under PRTOS) */
	/* Force IF=1 in saved RFLAGS so task resumes with virtual interrupts enabled */
	orq $0x200, (%rsp)
	pushq $GUEST_CS_SEL	/* CS */
	pushq %r11		/* RIP (return address) */

	/* Now stack has: [RIP, CS, RFLAGS, RSP, SS] = interrupt frame
	 * Same layout as when PRTOS delivers a virtual interrupt. */

	/* Save context frame (152 bytes = portCONTEXT_SIZE) */
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

	/* RIP copy from interrupt frame */
	movq portCONTEXT_SIZE(%rsp), %rax
	movq %rax, 24(%rsp)

	/* RFLAGS copy from interrupt frame */
	movq (portCONTEXT_SIZE+16)(%rsp), %rax
	movq %rax, 16(%rsp)

	/* Critical nesting count */
	movq ullCriticalNesting(%rip), %rax
	movq %rax, 8(%rsp)

	/* FPU context flag */
	movq ullPortTaskHasFPUContext(%rip), %rax
	movq %rax, 0(%rsp)

	/* Save stack to TCB */
	movq pxCurrentTCB(%rip), %rax
	movq %rsp, (%rax)

	/* Increment interrupt nesting */
	movq ullPortInterruptNesting(%rip), %rax
	incq %rax
	movq %rax, ullPortInterruptNesting(%rip)

	/* Switch context */
	call vTaskSwitchContext
	movq $0, ullPortYieldRequired(%rip)

	/* Decrement interrupt nesting */
	movq ullPortInterruptNesting(%rip), %rax
	decq %rax
	movq %rax, ullPortInterruptNesting(%rip)

	/* Restore (possibly new) task context */
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

	/* PRTOS virtual iret */
	int $0x83
