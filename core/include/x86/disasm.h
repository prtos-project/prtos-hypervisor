/*
 * FILE: disasm.h
 *
 * Basic disassembler
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_DISASM_H_
#define _PRTOS_ARCH_DISASM_H_

#ifndef _PRTOS_KERNEL_
#error Kernel file, do not include.
#endif

#define __UNKNOWN_INS -1
#define __CLI_INS 0
#define __STI_INS 1
#define __POP_INS 2

#define __REG_EAX 0
#define __REG_EBX 1
#define __REG_ECX 2
#define __REG_EDX 3
#define __REG_ESI 4
#define __REG_EDI 5
#define __REG_DS 6
#define __REG_ES 7
#define __REG_FS 8
#define __REG_GS 9

#define __OP_REG 0
#define __OP_INT 1
#define __OP_MEM 2

struct ins {
    prtos_u32_t rsv : 12, op1_t : 2, op2_t : 2, ins : 16;
    prtos_s32_t op1;
    prtos_s32_t op2;
};

extern prtos_s32_t disasm(prtos_u32_t eip, struct ins *i);

#endif
