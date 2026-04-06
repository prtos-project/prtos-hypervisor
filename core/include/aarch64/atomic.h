/*
 * FILE: atomic.h
 *
 * AArch64 atomic operations
 *
 * http://www.prtos.org/
 */

#ifndef _PRTOS_ARCH_ATOMIC_H_
#define _PRTOS_ARCH_ATOMIC_H_

typedef struct {
    volatile prtos_u32_t val;
} prtos_atomic_t;

#ifdef _PRTOS_KERNEL_

#define prtos_atomic_set(v, i) (((v)->val) = (i))
#define prtos_atomic_get(v) ((v)->val)

#define prtos_atomic_glear_mask(mask, addr)
#define prtos_atomic_setmask(mask, addr)

static inline void prtos_atomic_inc(prtos_atomic_t *v) {
    prtos_u32_t tmp;
    prtos_u32_t result;
    __asm__ __volatile__(
        "1: ldxr  %w0, [%2]\n"
        "   add   %w0, %w0, #1\n"
        "   stxr  %w1, %w0, [%2]\n"
        "   cbnz  %w1, 1b\n"
        : "=&r"(result), "=&r"(tmp)
        : "r"(&v->val)
        : "memory");
}

static inline void prtos_atomic_dec(prtos_atomic_t *v) {
    prtos_u32_t tmp;
    prtos_u32_t result;
    __asm__ __volatile__(
        "1: ldxr  %w0, [%2]\n"
        "   sub   %w0, %w0, #1\n"
        "   stxr  %w1, %w0, [%2]\n"
        "   cbnz  %w1, 1b\n"
        : "=&r"(result), "=&r"(tmp)
        : "r"(&v->val)
        : "memory");
}

static inline prtos_s32_t prtos_atomic_dec_and_test(prtos_atomic_t *v) {
    prtos_u32_t tmp;
    prtos_u32_t result;
    __asm__ __volatile__(
        "1: ldxr  %w0, [%2]\n"
        "   sub   %w0, %w0, #1\n"
        "   stxr  %w1, %w0, [%2]\n"
        "   cbnz  %w1, 1b\n"
        : "=&r"(result), "=&r"(tmp)
        : "r"(&v->val)
        : "memory");
    return result == 0;
}

#endif
#endif
