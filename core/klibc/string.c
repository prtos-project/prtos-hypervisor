/*
 * FILE: string.c
 *
 * String related functions
 *
 * www.prtos.org
 */

#include <stdc.h>
#include <arch/prtos_def.h>

void *memset(void *dst, prtos_s32_t s, prtos_u_size_t count) {
    register prtos_s8_t *a = dst;
    count++;
    while (--count) *a++ = s;
    return dst;
}

#ifndef __ARCH_MEMCPY

void *memcpy(void *dst, const void *src, prtos_u_size_t count) {
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

void *memcpy_phys(void *dst, const void *src, prtos_u32_t count) {
    return 0;
}

#endif

prtos_s32_t memcmp(const void *dst, const void *src, prtos_u_size_t count) {
    prtos_s32_t r;
    const prtos_s8_t *d = dst;
    const prtos_s8_t *s = src;
    ++count;
    while (--count) {
        if ((r = (*d - *s))) return r;
        ++d;
        ++s;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *aux = dst;
    while ((*aux++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dest, const char *src, prtos_u_size_t n) {
    prtos_s32_t j;

    memset(dest, 0, n);

    for (j = 0; j < n && src[j]; j++) dest[j] = src[j];

    if (j >= n) dest[n - 1] = 0;

    return dest;
}

char *strcat(char *s, const char *t) {
    char *dest = s;
    s += strlen(s);
    for (;;) {
        if (!(*s = *t)) break;
        ++s;
        ++t;
    }
    return dest;
}

char *strncat(char *s, const char *t, prtos_u_size_t n) {
    char *dest = s;
    register char *max;
    s += strlen(s);
    if ((max = s + n) == s) goto fini;
    for (;;) {
        if (!(*s = *t)) break;
        if (++s == max) break;
        ++t;
    }
    *s = 0;
fini:
    return dest;
}

prtos_s32_t strcmp(const char *s, const char *t) {
    char x;

    for (;;) {
        x = *s;
        if (x != *t) break;
        if (!x) break;
        ++s;
        ++t;
    }
    return ((prtos_s32_t)x) - ((prtos_s32_t)*t);
}

prtos_s32_t strncmp(const char *s1, const char *s2, prtos_u_size_t n) {
    register const prtos_u8_t *a = (const prtos_u8_t *)s1;
    register const prtos_u8_t *b = (const prtos_u8_t *)s2;
    register const prtos_u8_t *fini = a + n;

    while (a < fini) {
        register prtos_s32_t res = *a - *b;
        if (res) return res;
        if (!*a) return 0;
        ++a;
        ++b;
    }
    return 0;
}

prtos_u_size_t strlen(const char *s) {
    prtos_u32_t i;
    if (!s) return 0;
    for (i = 0; *s; ++s) ++i;
    return i;
}

char *strrchr(const char *t, prtos_s32_t c) {
    char ch;
    const char *l = 0;

    ch = c;
    for (;;) {
        if (*t == ch) l = t;
        if (!*t) return (char *)l;
        ++t;
    }

    return (char *)l;
}

char *strchr(const char *t, prtos_s32_t c) {
    register char ch;

    ch = c;
    for (;;) {
        if (*t == ch) break;
        if (!*t) return 0;
        ++t;
    }
    return (char *)t;
}

char *strstr(const char *haystack, const char *needle) {
    prtos_u_size_t nl = strlen(needle);
    prtos_u_size_t hl = strlen(haystack);
    prtos_s32_t i;
    if (!nl) goto found;
    if (nl > hl) return 0;
    for (i = hl - nl + 1; i; --i) {
        if (*haystack == *needle && !memcmp(haystack, needle, nl))
        found:
            return (char *)haystack;
        ++haystack;
    }
    return 0;
}

void *memmove(void *dst, const void *src, prtos_u_size_t count) {
    prtos_s8_t *a = dst;
    const prtos_s8_t *b = src;
    if (src != dst) {
        if (src > dst) {
            while (count--) *a++ = *b++;
        } else {
            a += count - 1;
            b += count - 1;
            while (count--) *a-- = *b--;
        }
    }
    return dst;
}
