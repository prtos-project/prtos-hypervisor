/*
 * FILE: linkage.h
 *
 * RISC-V 64-bit linkage macros
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_LINKAGE_H_
#define _PRTOS_ARCH_LINKAGE_H_

#ifndef _ASSEMBLY_
#define ALIGNMENT 8
#define ASM_ALIGN .align ALIGNMENT
#define __stdcall
#define __hypercall
#define ALIGNED_C
#endif


#ifdef __ASSEMBLY__

#define ROUNDUP(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define IS_ALIGNED(val, align) (!((val) & ((align) - 1)))
#define DIV_ROUND(n, d) (((n) + (d) / 2) / (d))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define ISOLATE_LSB(x) ((x) & -(x))
#define MASK_EXTR(v, m) (((v) & (m)) / ISOLATE_LSB(m))
#define MASK_INSR(v, m) (((v) * ISOLATE_LSB(m)) & (m))

#ifndef CODE_FILL
# define CODE_FILL
#endif

#ifndef DATA_ALIGN
# define DATA_ALIGN 0
#endif
#ifndef DATA_FILL
# define DATA_FILL
#endif

/* RISC-V function/data decoration macros */
.macro FUNC name
    .globl \name
    .type \name, @function
    .align 2
    \name:
.endm

.macro END name
    .size \name, . - \name
.endm

.macro FUNC_LOCAL name
    .type \name, @function
    .align 2
    \name:
.endm

.macro DATA name
    .globl \name
    .type \name, @object
    .align 3
    \name:
.endm

.macro DATA_LOCAL name
    .type \name, @object
    .align 3
    \name:
.endm

#endif /* __ASSEMBLY__ */
#endif
