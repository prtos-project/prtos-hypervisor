/*
 * FILE: stdlib.c
 *
 * Standard library definitions
 *
 * www.prtos.org
 */

#include <stdc.h>
unsigned long strtoul(const char *ptr, char **endptr, prtos_s32_t base) {
    prtos_s32_t neg = 0, overflow = 0;
    prtos_u32_t v = 0;
    const char *orig;
    const char *nptr = ptr;

    while (isspace(*nptr)) ++nptr;

    if (*nptr == '-') {
        neg = 1;
        nptr++;
    } else if (*nptr == '+')
        ++nptr;
    orig = nptr;
    if (base == 16 && nptr[0] == '0') goto skip0x;
    if (base) {
        register prtos_u32_t b = base - 2;
        if (b > 34) {
            return 0;
        }
    } else {
        if (*nptr == '0') {
            base = 8;
        skip0x:
            if ((nptr[1] == 'x' || nptr[1] == 'X') && isxdigit(nptr[2])) {
                nptr += 2;
                base = 16;
            }
        } else
            base = 10;
    }
    while (*nptr) {
        register unsigned char c = *nptr;
        c = (c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c <= '9' ? c - '0' : 0xff);
        if (c >= base) break; /* out of base */
        {
            register prtos_u32_t x = (v & 0xff) * base + c;
            register prtos_u32_t w = (v >> 8) * base + (x >> 8);
            if (w > (ULONG_MAX >> 8)) overflow = 1;
            v = (w << 8) + (x & 0xff);
        }
        ++nptr;
    }
    if (nptr == orig) { /* no conversion done */
        nptr = ptr;
        v = 0;
    }
    if (endptr) *endptr = (char *)nptr;
    if (overflow) {
        return ULONG_MAX;
    }
    return (neg ? -v : v);
}

#define ABS_LONG_MIN 2147483648UL

long strtol(const char *nptr, char **endptr, prtos_s32_t base) {
    prtos_s32_t neg = 0;
    prtos_u32_t v;
    const char *orig = nptr;

    while (isspace(*nptr)) nptr++;

    if (*nptr == '-' && isalnum(nptr[1])) {
        neg = -1;
        ++nptr;
    }
    v = strtoul(nptr, endptr, base);
    if (endptr && *endptr == nptr) *endptr = (char *)orig;
    if (v >= ABS_LONG_MIN) {
        if (v == ABS_LONG_MIN && neg) {
            return v;
        }
        return (neg ? LONG_MIN : LONG_MAX);
    }
    return (neg ? -v : v);
}

prtos_u64_t strtoull(const char *ptr, char **endptr, prtos_s32_t base) {
    prtos_s32_t neg = 0, overflow = 0;
    prtos_s64_t v = 0;
    const char *orig;
    const char *nptr = ptr;

    while (isspace(*nptr)) ++nptr;

    if (*nptr == '-') {
        neg = 1;
        nptr++;
    } else if (*nptr == '+')
        ++nptr;
    orig = nptr;
    if (base == 16 && nptr[0] == '0') goto skip0x;
    if (base) {
        register prtos_u32_t b = base - 2;
        if (b > 34) {
            return 0;
        }
    } else {
        if (*nptr == '0') {
            base = 8;
        skip0x:
            if (((*(nptr + 1) == 'x') || (*(nptr + 1) == 'X')) && isxdigit(nptr[2])) {
                nptr += 2;
                base = 16;
            }
        } else
            base = 10;
    }
    while (*nptr) {
        register unsigned char c = *nptr;
        c = (c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c <= '9' ? c - '0' : 0xff);
        if (c >= base) break; /* out of base */
        {
            register prtos_u32_t x = (v & 0x1ff) * base + c;
            register prtos_u64_t w = (v >> 8) * base + (x >> 8);
            if (w > (ULLONG_MAX >> 8)) overflow = 1;
            v = (w << 8) + (x & 0xff);
        }
        ++nptr;
    }
    if (nptr == orig) { /* no conversion done */
        nptr = ptr;
        v = 0;
    }
    if (endptr) *endptr = (char *)nptr;
    if (overflow) {
        return ULLONG_MAX;
    }
    return (neg ? -v : v);
}

prtos_s64_t strtoll(const char *nptr, char **endptr, prtos_s32_t base) {
    prtos_s32_t neg = 0;
    prtos_u64_t v;
    const char *orig = nptr;

    while (isspace(*nptr)) nptr++;

    if (*nptr == '-' && isalnum(nptr[1])) {
        neg = -1;
        nptr++;
    }
    v = strtoull(nptr, endptr, base);
    if (endptr && *endptr == nptr) *endptr = (char *)orig;
    if (v > LLONG_MAX) {
        if (v == 0x8000000000000000ull && neg) {
            return v;
        }
        return (neg ? LLONG_MIN : LLONG_MAX);
    }
    return (neg ? -v : v);
}

char *basename(char *path) {
    char *c;
again:
    if (!(c = strrchr(path, '/'))) return path;
    if (c[1] == 0) {
        if (c == path)
            return c;
        else {
            *c = 0;
            goto again;
        }
    }
    return c + 1;
}
