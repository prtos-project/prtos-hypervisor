/*
 * DO NOT MODIFY.
 *
 * This file was auto-generated from asm-offsets.s
 *
 */

#ifndef __ASM_OFFSETS_H__
#define __ASM_OFFSETS_H__

#define UREGS_X0 0 /* offsetof(struct cpu_user_regs, x0) */
#define UREGS_X1 8 /* offsetof(struct cpu_user_regs, x1) */
#define UREGS_LR 240 /* offsetof(struct cpu_user_regs, lr) */
#define UREGS_SP 248 /* offsetof(struct cpu_user_regs, sp) */
#define UREGS_PC 256 /* offsetof(struct cpu_user_regs, pc) */
#define UREGS_CPSR 264 /* offsetof(struct cpu_user_regs, cpsr) */
#define UREGS_ESR_el2 272 /* offsetof(struct cpu_user_regs, hsr) */
#define UREGS_SPSR_el1 288 /* offsetof(struct cpu_user_regs, spsr_el1) */
#define UREGS_SPSR_fiq 296 /* offsetof(struct cpu_user_regs, spsr_fiq) */
#define UREGS_SPSR_irq 300 /* offsetof(struct cpu_user_regs, spsr_irq) */
#define UREGS_SPSR_und 304 /* offsetof(struct cpu_user_regs, spsr_und) */
#define UREGS_SPSR_abt 308 /* offsetof(struct cpu_user_regs, spsr_abt) */
#define UREGS_SP_el0 312 /* offsetof(struct cpu_user_regs, sp_el0) */
#define UREGS_SP_el1 320 /* offsetof(struct cpu_user_regs, sp_el1) */
#define UREGS_ELR_el1 328 /* offsetof(struct cpu_user_regs, elr_el1) */
#define UREGS_kernel_sizeof 288 /* offsetof(struct cpu_user_regs, spsr_el1) */

#define CPUINFO_sizeof 352 /* sizeof(struct cpu_info) */
#define CPUINFO_flags 344 /* offsetof(struct cpu_info, flags) */

#define VCPU_arch_saved_context 640 /* offsetof(struct vcpu, arch.saved_context) */

#define INITINFO_stack 0 /* offsetof(struct init_info, stack) */

#define SMCCC_RES_a0 0 /* offsetof(struct arm_smccc_res, a0) */
#define SMCCC_RES_a2 16 /* offsetof(struct arm_smccc_res, a2) */
#define ARM_SMCCC_1_2_REGS_X0_OFFS 0 /* offsetof(struct arm_smccc_1_2_regs, a0) */
#define ARM_SMCCC_1_2_REGS_X2_OFFS 16 /* offsetof(struct arm_smccc_1_2_regs, a2) */
#define ARM_SMCCC_1_2_REGS_X4_OFFS 32 /* offsetof(struct arm_smccc_1_2_regs, a4) */
#define ARM_SMCCC_1_2_REGS_X6_OFFS 48 /* offsetof(struct arm_smccc_1_2_regs, a6) */
#define ARM_SMCCC_1_2_REGS_X8_OFFS 64 /* offsetof(struct arm_smccc_1_2_regs, a8) */
#define ARM_SMCCC_1_2_REGS_X10_OFFS 80 /* offsetof(struct arm_smccc_1_2_regs, a10) */
#define ARM_SMCCC_1_2_REGS_X12_OFFS 96 /* offsetof(struct arm_smccc_1_2_regs, a12) */
#define ARM_SMCCC_1_2_REGS_X14_OFFS 112 /* offsetof(struct arm_smccc_1_2_regs, a14) */
#define ARM_SMCCC_1_2_REGS_X16_OFFS 128 /* offsetof(struct arm_smccc_1_2_regs, a16) */


#endif
