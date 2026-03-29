/*
 * start.S - Para-virt FreeRTOS boot entry for RISC-V on PRTOS
 *
 * Entered in VS-mode by PRTOS hypervisor.
 * a0 = 0 (hartid), a1 = PCT physical address
 *
 * Registers the FreeRTOS trap dispatch in the PCT so the hypervisor
 * can deliver virtual IRQs (timer ticks).
 */

/* PCT arch offsets (must match partition_control_table_t) */
#define PCT_ARCH_TRAP_ENTRY_OFFSET  0  /* First field in pct_arch */

	.section .text.boot
	.globl _boot
_boot:
	/* a0 = 0 (hartid), a1 = PCT physical address */
	/* Save PCT address for C code to use */
	la t0, g_pct_addr
	sd a1, 0(t0)

	/* Set up stack pointer */
	la sp, stack_top

	/* Clear BSS */
	la t0, __bss_start
	la t1, __bss_end
1:
	bge t0, t1, 2f
	sd zero, 0(t0)
	addi t0, t0, 8
	j 1b
2:
	/* Re-save PCT address (may have been cleared by BSS zero) */
	la t0, g_pct_addr
	sd a1, 0(t0)

	/* Call main */
	call main

	/* Should not return */
_hang:
	wfi
	j _hang

.data
.align 3
.globl g_pct_addr
g_pct_addr:
	.quad 0

/*
 * _prtos_trap_dispatch - Virtual IRQ handler called by hypervisor
 *
 * The hypervisor redirects the guest's PC here when a virtual IRQ
 * (e.g., timer tick) is pending. We:
 *   1. Save volatile registers
 *   2. Call the FreeRTOS tick handler
 *   3. Check if context switch is needed
 *   4. Restore registers and do IRET (ecall a0=44) to return to original code
 */
	.globl _prtos_trap_dispatch
	.align 4
_prtos_trap_dispatch:
	/* Save caller-saved registers */
	addi sp, sp, -(18*8)
	sd ra,  0(sp)
	sd t0,  8(sp)
	sd t1,  16(sp)
	sd t2,  24(sp)
	sd a0,  32(sp)
	sd a1,  40(sp)
	sd a2,  48(sp)
	sd a3,  56(sp)
	sd a4,  64(sp)
	sd a5,  72(sp)
	sd a6,  80(sp)
	sd a7,  88(sp)
	sd t3,  96(sp)
	sd t4,  104(sp)
	sd t5,  112(sp)
	sd t6,  120(sp)
	sd s0,  128(sp)
	sd s1,  136(sp)

	/* Call FreeRTOS tick handler (handles timer tick and context switch) */
	call FreeRTOS_Tick_Handler

	/* Check if a yield/context switch was requested */
	la t0, ullPortYieldRequired
	ld t1, 0(t0)
	beqz t1, _no_switch

	/* Clear the yield flag */
	sd zero, 0(t0)

	/* Save full context and do context switch */
	call vTaskSwitchContext

_no_switch:
	/* Restore caller-saved registers */
	ld ra,  0(sp)
	ld t0,  8(sp)
	ld t1,  16(sp)
	ld t2,  24(sp)
	ld a0,  32(sp)
	ld a1,  40(sp)
	ld a2,  48(sp)
	ld a3,  56(sp)
	ld a4,  64(sp)
	ld a5,  72(sp)
	ld a6,  80(sp)
	ld a7,  88(sp)
	ld t3,  96(sp)
	ld t4,  104(sp)
	ld t5,  112(sp)
	ld t6,  120(sp)
	ld s0,  128(sp)
	ld s1,  136(sp)
	addi sp, sp, (18*8)

	/* IRET: ecall with a0 = NR_HYPERCALLS (44)
	 * The hypervisor restores the original PC, sstatus, and a0. */
	li a0, 44
	ecall

	/* Should not reach here */
	ret
