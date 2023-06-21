/*
 * FILE: checksum.h
 *
 * checksum algorithm
 *
 * www.prtos.org
 */

#ifndef _PRTOS_CHECKSUM_H_
#define _PRTOS_CHECKSUM_H_

#ifdef _PRTOS_KERNEL_
#include <assert.h>
#define RHALF(x) x
#else
#include <endianess.h>
#define ASSERT(a)
#endif

static inline prtos_u16_t calc_check_sum(prtos_u16_t *buffer, prtos_s32_t size) {
    prtos_u16_t sum = 0;
    ASSERT(!(size & 0x1));
    size >>= 1;
    while (size--) {
        sum += *buffer++;
        if (sum & 0xffff0000) {
            sum &= 0xffff;
            sum++;
        }
    }
    return ~(sum & 0xffff);
}

static inline prtos_s32_t is_valid_check_sum(prtos_u16_t *buffer, prtos_s32_t size) {
    prtos_u16_t sum = 0;
    ASSERT(!(size & 0x1));
    size >>= 1;
    while (size--) {
        sum += RHALF(*buffer);
        buffer++;
        if (sum & 0xffff0000) {
            sum &= 0xffff;
            sum++;
        }
    }
    return (sum == 0xffff) ? 1 : 0;
}

#endif
