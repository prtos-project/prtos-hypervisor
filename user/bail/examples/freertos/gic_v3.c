/*
 * GICv3 Virtual CPU Interface initialization for FreeRTOS on PRTOS (HW-Virt).
 *
 * The hypervisor (PRTOS/Xen at EL2) manages the physical GIC distributor
 * and redistributor. The guest at EL1 only interacts with the virtual CPU
 * interface via ICC_* system registers, backed by ICH_HCR_EL2.En=1.
 */
#include <stdint.h>
#include "board.h"
#include "gic_v3.h"

void gic_v3_initialize(void)
{
	/* Ensure system register interface is enabled (ICC_SRE_EL1.SRE = 1).
	 * This should already be set by the hypervisor (ICC_SRE_EL2.Enable=1),
	 * but set it explicitly for safety. */
	uint32_t sre = icc_read_sre();
	sre |= 0x1; /* SRE bit */
	icc_write_sre(sre);
	__asm volatile("isb");

	/* Set priority mask to lowest priority (allow all interrupts) */
	icc_write_pmr(0xFF);

	/* Set binary point to 0 (all bits used for preemption priority) */
	icc_write_bpr1(0);

	/* Enable Group 1 interrupts */
	icc_write_igrpen1(0x1);

	__asm volatile("isb");
}
