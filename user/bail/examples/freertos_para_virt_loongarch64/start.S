/*
 * start.S - Para-virt FreeRTOS boot entry for LoongArch64 on PRTOS
 *
 * Entered at PLV3 by PRTOS hypervisor via JMP_PARTITION.
 * a0 = PCT physical address
 */

	.section .text.boot
	.globl _boot
_boot:
	/* a0 = PCT physical address */
	la.pcrel $t0, g_pct_addr
	st.d $a0, $t0, 0

	/* Set up stack pointer */
	la.pcrel $sp, stack_top

	/* Clear BSS */
	la.pcrel $t0, __bss_start
	la.pcrel $t1, __bss_end
1:
	bgeu $t0, $t1, 2f
	st.d $zero, $t0, 0
	addi.d $t0, $t0, 8
	b 1b
2:
	/* Re-save PCT address (may have been zeroed by BSS clear) */
	la.pcrel $t0, g_pct_addr
	st.d $a0, $t0, 0

	/* Call main */
	bl main

	/* Should not return */
_hang:
	b _hang

.data
.align 3
.globl g_pct_addr
g_pct_addr:
	.quad 0

.globl g_tick_count
g_tick_count:
	.quad 0

/*
 * _prtos_trap_dispatch is now in portASM.S (full context save/restore
 * with ISR-driven context switching support).
 */
