/*
 * FILE: head.S
 *
 * RISC-V 64-bit MMU / page table setup
 * Placeholder - PRTOS riscv64 runs with identity mapping initially
 *
 * www.prtos.org
 */

#include <linkage.h>
#include <arch/prtos_def.h>

/* Currently PRTOS riscv64 runs with simple identity mapping (no virtual
 * memory offset). The QEMU virt machine loads us at a known physical
 * address, and we operate directly with physical addresses.
 * Full Sv39 page table setup will be added when needed. */

.section .boot.text, "ax"

/* Placeholder: nothing to do for now */
