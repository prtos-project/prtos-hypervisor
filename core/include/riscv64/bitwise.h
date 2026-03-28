/*
 * FILE: bitwise.h
 *
 * RISC-V 64-bit basic bit operations
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_BITWISE_H_
#define _PRTOS_ARCH_BITWISE_H_

#define ARCH_HAS_FFS

static __inline__ prtos_s32_t _ffs(prtos_s32_t x) {
    if (!x) return -1;
    return __builtin_ctz((unsigned int)x);
}

#define ARCH_HAS_FFZ

static __inline__ prtos_s32_t _ffz(prtos_s32_t x) {
    return _ffs(~x);
}

#define ARCH_HAS_FLS

static __inline__ prtos_s32_t _fls(prtos_s32_t x) {
    if (!x) return -1;
    return 31 - __builtin_clz((unsigned int)x);
}

#define ARCH_HAS_SET_BIT

static inline void _set_bit(prtos_s32_t nr, volatile prtos_u32_t *addr) {
    *addr |= (1U << nr);
}

#define ARCH_HAS_CLEAR_BIT

static inline void _clear_bit(prtos_s32_t nr, volatile prtos_u32_t *addr) {
    *addr &= ~(1U << nr);
}

#endif
