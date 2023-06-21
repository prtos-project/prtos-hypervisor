/*
 * FILE: linkage.h
 *
 * Definition of some macros to ease the interoperatibility between
 * assembly and C
 *
 * www.prtos.org
 */

#ifndef _PRTOS_LINKAGE_H_
#define _PRTOS_LINKAGE_H_

#include __PRTOS_INCFLD(arch/linkage.h)
#include __PRTOS_INCFLD(arch/paging.h)

#define PAGE_ALIGN .align PAGE_SIZE

#ifndef _ASSEMBLY_
#define __NOINLINE __attribute__((noinline))
#define __PACKED __attribute__((__packed__))
#define __WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#endif

#define SYMBOL_NAME(X) X
#define SYMBOL_NAME_LABEL(X) X##:

#define ENTRY(name)           \
    .globl SYMBOL_NAME(name); \
    ASM_ALIGN;                \
    SYMBOL_NAME_LABEL(name)

#define __STR(x) #x
#define TO_STR(x) __STR(x)

#endif
