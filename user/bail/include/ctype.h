/*
 * FILE: ctype.h
 *
 * c types
 *
 * www.prtos.org
 */

#ifndef _BAIL_CTYPE_H_
#define _BAIL_CTYPE_H_

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

#endif
