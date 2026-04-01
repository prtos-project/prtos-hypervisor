/*
 * FILE: vmx_asm.S
 *
 * VMX guest enter/exit assembly for amd64
 *
 * Guest GP registers are stored in vmx_state->guest_regs[] which starts
 * at offset GREGS_OFF (32) into struct vmx_state:
 *   vmcs_phys(8) + vmcs_virt(8) + eptp(8) + ept_pml4(8) = 32 bytes
 *
 * http://www.prtos.org/
 */

/* Offset of guest_regs[] within struct vmx_state */
#define GREGS_OFF   32
/* VMCS field encoding for HOST_RSP */
#define VMCS_HOST_RSP_ENC  0x6C14

.text

/*
 * int vmx_guest_enter(struct vmx_state *vmx)
 *
 * Enter guest via VMRESUME. Returns 0 on VM exit, -1 on failure.
 * %rdi = pointer to struct vmx_state
 */
.globl vmx_guest_enter
.align 16
vmx_guest_enter:
        /* Save callee-saved registers */
        pushq %rbx
        pushq %rbp
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15
        pushq %rdi              /* save vmx_state pointer */

        /* Update HOST_RSP in VMCS to current RSP (after all pushes).
         * On VM exit the CPU restores RSP to this value, so
         * vmx_vmexit_handler can find the saved context. */
        movq  $VMCS_HOST_RSP_ENC, %rbx
        movq  %rsp, %rcx
        vmwrite %rcx, %rbx

        /* Load guest GP registers from vmx->guest_regs[] (at offset GREGS_OFF) */
        leaq  GREGS_OFF(%rdi), %rdi
        movq    0(%rdi), %rax
        movq   16(%rdi), %rdx
        movq   24(%rdi), %rbx
        movq   40(%rdi), %rbp
        movq   48(%rdi), %rsi
        movq   64(%rdi), %r8
        movq   72(%rdi), %r9
        movq   80(%rdi), %r10
        movq   88(%rdi), %r11
        movq   96(%rdi), %r12
        movq  104(%rdi), %r13
        movq  112(%rdi), %r14
        movq  120(%rdi), %r15
        movq    8(%rdi), %rcx   /* RCX */
        movq   56(%rdi), %rdi   /* RDI last (clobbers pointer) */

        vmresume

        /* VMRESUME failed - fall through */
        jmp vmx_enter_fail

/*
 * int vmx_first_launch(struct vmx_state *vmx)
 *
 * Enter guest via VMLAUNCH (first entry). Returns 0 on VM exit, -1 on failure.
 * %rdi = pointer to struct vmx_state
 */
.globl vmx_first_launch
.align 16
vmx_first_launch:
        pushq %rbx
        pushq %rbp
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15
        pushq %rdi

        movq  $VMCS_HOST_RSP_ENC, %rbx
        movq  %rsp, %rcx
        vmwrite %rcx, %rbx

        leaq  GREGS_OFF(%rdi), %rdi
        movq    0(%rdi), %rax
        movq   16(%rdi), %rdx
        movq   24(%rdi), %rbx
        movq   40(%rdi), %rbp
        movq   48(%rdi), %rsi
        movq   64(%rdi), %r8
        movq   72(%rdi), %r9
        movq   80(%rdi), %r10
        movq   88(%rdi), %r11
        movq   96(%rdi), %r12
        movq  104(%rdi), %r13
        movq  112(%rdi), %r14
        movq  120(%rdi), %r15
        movq    8(%rdi), %rcx
        movq   56(%rdi), %rdi

        vmlaunch

        /* VMLAUNCH failed */
        jmp vmx_enter_fail

/*
 * vmx_vmexit_handler
 *
 * HOST_RIP target. On VM exit the CPU loads:
 *   RSP = HOST_RSP (set to stack pointer after all pushes above)
 *   RIP = this function
 *
 * Stack layout at HOST_RSP (= RSP on entry):
 *   [RSP+ 0] = saved vmx_state pointer
 *   [RSP+ 8] = saved r15
 *   [RSP+16] = saved r14
 *   [RSP+24] = saved r13
 *   [RSP+32] = saved r12
 *   [RSP+40] = saved rbp
 *   [RSP+48] = saved rbx
 *   [RSP+56] = return address (from call vmx_guest_enter/vmx_first_launch)
 */
.globl vmx_vmexit_handler
.align 16
vmx_vmexit_handler:
        /* Temporarily save guest RDI on stack */
        pushq %rdi

        /* Load vmx_state pointer (at RSP+8 after the push above) */
        movq  8(%rsp), %rdi

        /* Save guest GP regs into vmx->guest_regs[] */
        leaq  GREGS_OFF(%rdi), %rdi
        movq  %rax,   0(%rdi)
        movq  %rcx,   8(%rdi)
        movq  %rdx,  16(%rdi)
        movq  %rbx,  24(%rdi)
        movq  %rbp,  40(%rdi)
        movq  %rsi,  48(%rdi)
        movq  %r8,   64(%rdi)
        movq  %r9,   72(%rdi)
        movq  %r10,  80(%rdi)
        movq  %r11,  88(%rdi)
        movq  %r12,  96(%rdi)
        movq  %r13, 104(%rdi)
        movq  %r14, 112(%rdi)
        movq  %r15, 120(%rdi)

        /* Recover guest RDI from stack and save it */
        popq  %rax
        movq  %rax,  56(%rdi)

        /* Restore callee-saved registers */
        popq  %rdi              /* vmx_state pointer (discard) */
        popq  %r15
        popq  %r14
        popq  %r13
        popq  %r12
        popq  %rbp
        popq  %rbx

        /* Return 0 = successful VM exit */
        xorl  %eax, %eax
        ret

vmx_enter_fail:
        /* VMLAUNCH/VMRESUME failed. RDI has guest value.
         * Stack still has: vmx_ptr, r15, r14, r13, r12, rbp, rbx, ret_addr.
         * Pop them to restore callee-saved registers. */
        popq  %rdi              /* vmx_state ptr */
        popq  %r15
        popq  %r14
        popq  %r13
        popq  %r12
        popq  %rbp
        popq  %rbx

        /* Return -1 = failure */
        movq  $-1, %rax
        ret
