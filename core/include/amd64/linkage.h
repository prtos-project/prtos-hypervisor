/*
 * FILE: linkage.h
 *
 * Linkage macros for amd64
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_LINKAGE_H_
#define _PRTOS_ARCH_LINKAGE_H_

#ifndef _ASSEMBLY_
#define ALIGNMENT 8
#define ASM_ALIGN .align ALIGNMENT
#define __stdcall
#define __hypercall __attribute__((noinline, section(".text")))
#define ALIGNED_C __attribute__((aligned(8)))
#endif

#endif
