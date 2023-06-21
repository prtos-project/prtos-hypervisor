/*
 * FILE: bitwise.h
 *
 * Some basic bit operations
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_BITWISE_H_
#define _PRTOS_ARCH_BITWISE_H_

#define ARCH_HAS_FFS

static __inline__ prtos_s32_t _ffs(prtos_s32_t x) {
    prtos_s32_t r;

    __asm__ __volatile__("bsfl %1,%0\n\t"
                         "jnz 1f\n\t"
                         "movl $-1,%0\n"
                         "1:"
                         : "=r"(r)
                         : "g"(x));
    return r;
}

#define ARCH_HAS_FFZ

static __inline__ prtos_s32_t _ffz(prtos_s32_t x) {
    prtos_s32_t r;

    __asm__ __volatile__("bsfl %1,%0\n\t"
                         "jnz 1f\n\t"
                         "movl $-1,%0\n"
                         "1:"
                         : "=r"(r)
                         : "g"(~x));
    return r;
}

#define ARCH_HAS_FLS

static __inline__ prtos_s32_t _fls(prtos_s32_t x) {
    prtos_s32_t r;

    __asm__ __volatile__("bsrl %1,%0\n\t"
                         "jnz 1f\n\t"
                         "movl $-1,%0\n"
                         "1:"
                         : "=r"(r)
                         : "g"(x));
    return r;
}

#define ARCH_HAS_SET_BIT

static inline void _set_bit(prtos_s32_t nr, volatile prtos_u32_t *addr) {
    __asm__ __volatile__("btsl %1,%0" : "=m"((*(volatile prtos_s32_t *)addr)) : "Ir"(nr));
}

#define ARCH_HAS_CLEAR_BIT

static inline void _clear_bit(prtos_s32_t nr, volatile prtos_u32_t *addr) {
    __asm__ __volatile__("btrl %1,%0" : "=m"((*(volatile prtos_s32_t *)addr)) : "Ir"(nr));
}

#endif
