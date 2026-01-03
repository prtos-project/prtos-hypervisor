/*
 * FILE: string.c
 *
 * String related functions
 *
 * www.prtos.org
 */

#include <stdc.h>
#include <arch/prtos_def.h>

#ifdef __ARCH_MEMCPY

static inline void *_mem_cpy1(prtos_s8_t *dst, const prtos_s8_t *src, prtos_u_size_t count) {
    register prtos_s8_t *d = dst;
    register const prtos_s8_t *s = src;
    ++count;
    while (--count) {
        *d = *s;
        ++d;
        ++s;
    }
    return dst;
}

static inline void *_mem_cpy2(prtos_s16_t *dst, const prtos_s16_t *src, prtos_u_size_t count) {
    register prtos_s16_t *d = dst;
    register const prtos_s16_t *s = src;
    ++count;
    while (--count) {
        *d = *s;
        ++d;
        ++s;
    }
    return dst;
}

static inline void *_mem_cpy4(prtos_s32_t *dst, const prtos_s32_t *src, prtos_u_size_t count) {
    register prtos_s32_t *d = dst;
    register const prtos_s32_t *s = src;
    ++count;
    while (--count) {
        *d = *s;
        ++d;
        ++s;
    }
    return dst;
}

static inline void *_mem_cpy8(prtos_s64_t *dst, const prtos_s64_t *src, prtos_u_size_t count) {
    register prtos_s64_t *d = dst;
    register const prtos_s64_t *s = src;
    ++count;
    while (--count) {
        *d = *s;
        ++d;
        ++s;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, prtos_u_size_t count) {
    prtos_s8_t *d, *s;
    prtos_s32_t r;
    d = (prtos_s8_t *)dst;
    s = (prtos_s8_t *)src;
    // Are both buffers aligned to 8 bytes?
    if (!(((prtos_address_t)dst | (prtos_address_t)src) & 7)) {
        r = count & 7;
        _mem_cpy8((prtos_s64_t *)d, (prtos_s64_t *)s, count >> 3);
        if (!r) return dst;
        d = &d[count - r];
        s = &s[count - r];
        count = r;
    }
    // Are both buffers aligned to 4 bytes?
    if (!(((prtos_address_t)dst | (prtos_address_t)src) & 3)) {
        r = count & 3;
        _mem_cpy4((prtos_s32_t *)d, (prtos_s32_t *)s, count >> 2);
        if (!r) return dst;
        d = &d[count - r];
        s = &s[count - r];
        count = r;
    }
    // Are both buffers aligned to 2 bytes?
    if (!(((prtos_address_t)dst | (prtos_address_t)src) & 1)) {
        r = count & 1;
        _mem_cpy2((prtos_s16_t *)d, (prtos_s16_t *)s, count >> 1);
        if (!r) return dst;
        d = &d[count - r];
        s = &s[count - r];
        count = r;
    }

    // Copying the buffers byte per byte
    return _mem_cpy1(d, s, count);
}

#endif
