/*
 * FILE: atomic.h
 *
 * atomic operations
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_ATOMIC_H_
#define _PRTOS_ARCH_ATOMIC_H_

typedef struct {
    volatile prtos_u32_t val;
} prtos_atomic_t;

//#ifdef _PRTOS_KERNEL_

#define prtos_atomic_set(v, i) (((v)->val) = (i))

#define prtos_atomic_get(v) ((v)->val)

#define prtos_atomic_glear_mask(mask, addr) __asm__ __volatile__("andl %0,%1" : : "r"(~(mask)), "m"(*addr) : "memory")

#define prtos_atomic_setmask(mask, addr) __asm__ __volatile__("orl %0,%1" : : "r"(mask), "m"(*(addr)) : "memory")

static inline void prtos_atomic_inc(prtos_atomic_t *v) {
    __asm__ __volatile__("incl %0" : "+m"(v->val));
}

static inline void prtos_atomic_dec(prtos_atomic_t *v) {
    __asm__ __volatile__("decl %0" : "+m"(v->val));
}

static inline prtos_s32_t prtos_atomic_dec_and_test(prtos_atomic_t *v) {
    prtos_u8_t c;

    __asm__ __volatile__("decl %0; sete %1" : "+m"(v->val), "=qm"(c) : : "memory");
    return c != 0;
}

//#endif
#endif
