/*
 * FILE: atomic.h
 *
 * LoongArch 64-bit atomic operations
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
    __asm__ __volatile__("amadd.w $zero, %1, %0"
                         : "+ZB"(v->val) : "r"(1) : "memory");
}

static inline void prtos_atomic_dec(prtos_atomic_t *v) {
    __asm__ __volatile__("amadd.w $zero, %1, %0"
                         : "+ZB"(v->val) : "r"(-1) : "memory");
}

static inline prtos_s32_t prtos_atomic_dec_and_test(prtos_atomic_t *v) {
    prtos_s32_t prev;
    __asm__ __volatile__("amadd.w %0, %2, %1"
                         : "=&r"(prev), "+ZB"(v->val)
                         : "r"(-1)
                         : "memory");
    return (prev - 1) == 0;
}

#endif
#endif
