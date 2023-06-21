/*
 * FILE: bitwise.h
 *
 * Some basic bit operations
 *
 * www.prtos.org
 */

#ifndef _PRTOS_BITWISE_H_
#define _PRTOS_BITWISE_H_

#include __PRTOS_INCFLD(arch/bitwise.h)

#ifndef ARCH_HAS_FFS

static __inline__ prtos_s32_t _Ffs(prtos_s32_t x) {
    prtos_s32_t r = 0;

    if (!x) return -1;
    if (!(x & 0xffff)) {
        x >>= 16;
        r += 16;
    }
    if (!(x & 0xff)) {
        x >>= 8;
        r += 8;
    }
    if (!(x & 0xf)) {
        x >>= 4;
        r += 4;
    }
    if (!(x & 3)) {
        x >>= 2;
        r += 2;
    }
    if (!(x & 1)) {
        x >>= 1;
        r += 1;
    }
    return r;
}

#endif

#ifndef ARCH_HAS_FFZ

#define _ffz(x) _Ffs(~(x))

#endif

#ifndef ARCH_HAS_FLS

static __inline__ prtos_s32_t _fls(prtos_s32_t x) {
    prtos_s32_t r = 31;

    if (!x) return -1;
    if (!(x & 0xffff0000u)) {
        x <<= 16;
        r -= 16;
    }
    if (!(x & 0xff000000u)) {
        x <<= 8;
        r -= 8;
    }
    if (!(x & 0xf0000000u)) {
        x <<= 4;
        r -= 4;
    }
    if (!(x & 0xc0000000u)) {
        x <<= 2;
        r -= 2;
    }
    if (!(x & 0x80000000u)) {
        x <<= 1;
        r -= 1;
    }
    return r;
}

#endif

#ifndef ARCH_HAS_SET_BIT

static inline void _SetBit(prtos_s32_t nr, volatile prtos_u32_t *addr) {
    (*addr) |= (1 << nr);
}

#endif

#ifndef ARCH_HAS_CLEAR_BIT

static inline void _ClearBit(prtos_s32_t nr, volatile prtos_u32_t *addr) {
    (*addr) &= ~(1 << nr);
}

#endif

#endif
