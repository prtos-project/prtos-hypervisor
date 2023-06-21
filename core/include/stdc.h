/*
 * FILE: stdc.h
 *
 * KLib's standard C functions definition
 *
 * www.prtos.org
 */

#ifndef _PRTOS_STDC_H_
#define _PRTOS_STDC_H_

#ifdef _PRTOS_KERNEL_
#include <linkage.h>

#ifndef __SCHAR_MAX__
#define __SCHAR_MAX__ 127
#endif
#ifndef __SHRT_MAX__
#define __SHRT_MAX__ 32767
#endif
#ifndef __INT_MAX__
#define __INT_MAX__ 2147483647
#endif

#ifndef __LONG_MAX__
#define __LONG_MAX__ 2147483647L
#endif

#define INT_MIN (-1 - INT_MAX)
#define INT_MAX (__INT_MAX__)
#define UINT_MAX (INT_MAX * 2U + 1U)

#define LONG_MIN (-1L - LONG_MAX)
#define LONG_MAX ((__LONG_MAX__) + 0L)
#define ULONG_MAX (LONG_MAX * 2UL + 1UL)

#define LLONG_MAX 9223372036854775807LL
#define LLONG_MIN (-LLONG_MAX - 1LL)

/* Maximum value an `unsigned long long int' can hold.  (Minimum is 0.)  */
#define ULLONG_MAX 18446744073709551615ULL

static inline prtos_s32_t isdigit(prtos_s32_t ch) {
    return (prtos_u32_t)(ch - '0') < 10u;
}

static inline prtos_s32_t isspace(prtos_s32_t ch) {
    return (prtos_u32_t)(ch - 9) < 5u || ch == ' ';
}

static inline prtos_s32_t isxdigit(prtos_s32_t ch) {
    return (prtos_u32_t)(ch - '0') < 10u || (prtos_u32_t)((ch | 0x20) - 'a') < 6u;
}

static inline prtos_s32_t isalnum(prtos_s32_t ch) {
    return (prtos_u32_t)((ch | 0x20) - 'a') < 26u || (prtos_u32_t)(ch - '0') < 10u;
}

typedef __builtin_va_list va_list;

#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)

#undef NULL
#define NULL ((void *)0)

#undef OFFSETOF
#ifdef __compiler_offsetof
#define OFFSETOF(_type, _member) __compiler_offsetof(_type, _member)
#else
#define OFFSETOF(_type, _member) ((prtos_u_size_t) & ((_type *)0)->_member)
#endif

#define EOF (-1)

extern prtos_s32_t kprintf(const char *, ...);
extern prtos_s32_t eprintf(const char *, ...);
extern prtos_s32_t vprintf(const char *fmt, va_list args);
extern prtos_s32_t sprintf(char *s, char const *fmt, ...);
extern prtos_s32_t snprintf(char *s, prtos_s32_t n, const char *fmt, ...);
extern void *memmove(void *, const void *, prtos_u_size_t);
extern unsigned long strtoul(const char *, char **, prtos_s32_t);
extern long strtol(const char *, char **, prtos_s32_t);
extern prtos_s64_t strtoll(const char *nptr, char **endptr, prtos_s32_t base);
extern prtos_u64_t strtoull(const char *ptr, char **endptr, prtos_s32_t base);
extern char *basename(char *path);
extern prtos_s32_t memcmp(const void *, const void *, prtos_u_size_t);
extern void *memcpy(void *, const void *, prtos_u_size_t);
extern void *memcpy_phys(void *dst, const void *src, prtos_u32_t count);
extern void *memset(void *, prtos_s32_t, prtos_u_size_t);
extern char *strcat(char *, const char *);
extern char *strncat(char *s, const char *t, prtos_u_size_t n);
extern char *strchr(const char *, prtos_s32_t);
extern prtos_s32_t strcmp(const char *, const char *);
extern prtos_s32_t strncmp(const char *, const char *, prtos_u_size_t);
extern char *strcpy(char *, const char *);
extern char *strncpy(char *dst, const char *src, prtos_u_size_t n);
extern prtos_u_size_t strlen(const char *);
extern char *strrchr(const char *, prtos_s32_t);
extern char *strstr(const char *, const char *);

// Non-standard functions
typedef void (*wr_mem_t)(prtos_u32_t *, prtos_u32_t);
typedef prtos_u32_t (*rd_mem_t)(prtos_u32_t *);

static inline void unalign_memcpy(prtos_u8_t *dst, prtos_u8_t *src, prtos_s_size_t size, rd_mem_t src_r, rd_mem_t dst_r, wr_mem_t dst_w) {
    prtos_u32_t l_scr_w, l_dst_w;
    prtos_s32_t c1, c2, e;

    for (e = 0, c1 = (prtos_u32_t)src & 0x3, c2 = (prtos_u32_t)dst & 0x3; e < size; src++, dst++, c1 = (c1 + 1) & 0x3, c2 = (c2 + 1) & 0x3, e++) {
        l_scr_w = src_r((prtos_u32_t *)((prtos_u32_t)src & ~0x3));
#ifdef CONFIG_TARGET_LITTLE_ENDIAN
        l_dst_w = l_scr_w & (0xff << ((c1 & 0x3) << 3));
        l_dst_w >>= ((c1 & 0x3) << 3);
        l_dst_w <<= ((c2 & 0x3) << 3);

        l_dst_w |= (dst_r((prtos_u32_t *)((prtos_u32_t)dst & ~0x3)) & ~(0xff << ((c2 & 0x3) << 3)));
#else
        l_dst_w = l_scr_w & (0xff000000 >> ((c1 & 0x3) << 3));
        l_dst_w <<= ((c1 & 0x3) << 3);
        l_dst_w >>= ((c2 & 0x3) << 3);

        l_dst_w |= (dst_r((prtos_u32_t *)((prtos_u32_t)dst & ~0x3)) & ~(0xff000000 >> ((c2 & 0x3) << 3)));
#endif
        dst_w((prtos_u32_t *)((prtos_u32_t)dst & ~0x3), l_dst_w);
    }
}

#define FILL_TAB(x) [x] = #x
#define PWARN kprintf
#endif
#endif
