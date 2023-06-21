/*
 * FILE: arch_types.h
 *
 * Types defined by the architecture
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_TYPES_H_
#define _PRTOS_ARCH_TYPES_H_

#ifndef __ASSEMBLY__

// Basic types
typedef unsigned char prtos_u8_t;
#define MAX_U8 0xFF
typedef char prtos_s8_t;
#define MAX_S8 0x7F
typedef unsigned short prtos_u16_t;
#define MAX_U16 0xFFFF
typedef short prtos_s16_t;
#define MAX_S16 0x7FFF
typedef unsigned int prtos_u32_t;
#define MAX_U32 0xFFFFFFFF
typedef int prtos_s32_t;
#define MAX_S32 0x7FFFFFFF
typedef unsigned long long prtos_u64_t;
#define MAX_U64 0xFFFFFFFFFFFFFFFFULL
typedef long long prtos_s64_t;
#define MAX_S64 0x7FFFFFFFFFFFFFFFLL

// Extended types
#define PRTOS_LOG2_WORD_SZ 5
typedef prtos_s64_t prtos_time_t;
#define MAX_PRTOSTIME MAX_S64
typedef long prtos_long_t;
typedef prtos_u32_t prtos_word_t;
typedef prtos_u32_t prtos_id_t;
typedef prtos_u32_t prtos_address_t;
typedef prtos_u16_t prtos_io_address_t;
typedef prtos_u32_t prtos_u_size_t;
typedef prtos_s32_t prtos_s_size_t;

#define PRNT_ADDR_FMT "l"
#define PTR2ADDR(x) ((prtos_word_t)x)
#define ADDR2PTR(x) ((void *)((prtos_word_t)x))

#ifdef _PRTOS_KERNEL_

// Extended internal types
typedef prtos_s64_t hw_time_t;

#endif /*_PRTOS_KERNEL_*/

#endif /*__ASSEMBLY__*/
#endif /*_PRTOS_ARCH_TYPES_H_*/
