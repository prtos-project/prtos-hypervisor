/* PRTOS library utilities - consolidated */
/* === BEGIN INLINED: bitmap.c === */
#include <prtos_prtos_config.h>
/*
 * lib/bitmap.c
 * Helper functions for bitmap.h.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */
#include <prtos_types.h>
#include <prtos_errno.h>
#include <prtos_bitmap.h>
#include <prtos_bitops.h>
#include <prtos_cpumask.h>
#include <prtos_guest_access.h>
#include <prtos_lib.h>
#include <asm_byteorder.h>

/*
 * bitmaps provide an array of bits, implemented using an an
 * array of unsigned longs.  The number of valid bits in a
 * given bitmap does _not_ need to be an exact multiple of
 * BITS_PER_LONG.
 *
 * The possible unused bits in the last, partially used word
 * of a bitmap are 'don't care'.  The implementation makes
 * no particular effort to keep them zero.  It ensures that
 * their value will not affect the results of any operation.
 * The bitmap operations that return Boolean (bitmap_empty,
 * for example) or scalar (bitmap_weight, for example) results
 * carefully filter out these unused bits from impacting their
 * results.
 *
 * These operations actually hold to a slightly stronger rule:
 * if you don't input any bitmaps to these ops that have some
 * unused bits set, then they won't output any set unused bits
 * in output bitmaps.
 *
 * The byte ordering of bitmaps is more natural on little
 * endian architectures.  See the big-endian headers
 * include/asm-ppc64/bitops.h and include/asm-s390/bitops.h
 * for the best explanations of this ordering.
 */

/*
 * If a bitmap has a number of bits which is not a multiple of 8 then
 * the last few bits of the last byte of the bitmap can be
 * unexpectedly set which can confuse consumers (e.g. in the tools)
 * who also round up their loops to 8 bits. Ensure we clear those left
 * over bits so as to prevent surprises.
 */
static void clamp_last_byte(uint8_t *bp, unsigned int nbits)
{
	unsigned int remainder = nbits % 8;

	if (remainder)
		bp[nbits/8] &= (1U << remainder) - 1;
}

int __bitmap_empty(const unsigned long *bitmap, unsigned int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if (bitmap[k] & BITMAP_LAST_WORD_MASK(bits))
			return 0;

	return 1;
}
EXPORT_SYMBOL(__bitmap_empty);

int __bitmap_full(const unsigned long *bitmap, unsigned int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (~bitmap[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if (~bitmap[k] & BITMAP_LAST_WORD_MASK(bits))
			return 0;

	return 1;
}
EXPORT_SYMBOL(__bitmap_full);

int __bitmap_equal(const unsigned long *bitmap1,
                   const unsigned long *bitmap2, unsigned int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] != bitmap2[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] ^ bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return 0;

	return 1;
}
EXPORT_SYMBOL(__bitmap_equal);

void __bitmap_complement(unsigned long *dst, const unsigned long *src, unsigned int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		dst[k] = ~src[k];

	if (bits % BITS_PER_LONG)
		dst[k] = ~src[k] & BITMAP_LAST_WORD_MASK(bits);
}
EXPORT_SYMBOL(__bitmap_complement);

void __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
                  const unsigned long *bitmap2, unsigned int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] & bitmap2[k];
}
EXPORT_SYMBOL(__bitmap_and);

void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
                 const unsigned long *bitmap2, unsigned int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] | bitmap2[k];
}
EXPORT_SYMBOL(__bitmap_or);

void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
                  const unsigned long *bitmap2, unsigned int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] ^ bitmap2[k];
}
EXPORT_SYMBOL(__bitmap_xor);

void __bitmap_andnot(unsigned long *dst, const unsigned long *bitmap1,
                     const unsigned long *bitmap2, unsigned int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] & ~bitmap2[k];
}
EXPORT_SYMBOL(__bitmap_andnot);

int __bitmap_intersects(const unsigned long *bitmap1,
                        const unsigned long *bitmap2, unsigned int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] & bitmap2[k])
			return 1;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] & bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return 1;
	return 0;
}
EXPORT_SYMBOL(__bitmap_intersects);

int __bitmap_subset(const unsigned long *bitmap1,
                    const unsigned long *bitmap2, unsigned int bits)
{
	int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] & ~bitmap2[k])
			return 0;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] & ~bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return 0;
	return 1;
}
EXPORT_SYMBOL(__bitmap_subset);

unsigned int __bitmap_weight(const unsigned long *bitmap, unsigned int bits)
{
	unsigned int k, w = 0, lim = bits / BITS_PER_LONG;

	for (k = 0; k < lim; k++)
		w += hweight_long(bitmap[k]);

	if (bits % BITS_PER_LONG)
		w += hweight_long(bitmap[k] & BITMAP_LAST_WORD_MASK(bits));

	return w;
}
EXPORT_SYMBOL(__bitmap_weight);



/**
 *	bitmap_find_free_region - find a contiguous aligned mem region
 *	@bitmap: an array of unsigned longs corresponding to the bitmap
 *	@bits: number of bits in the bitmap
 *	@order: region size to find (size is actually 1<<order)
 *
 * This is used to allocate a memory region from a bitmap.  The idea is
 * that the region has to be 1<<order sized and 1<<order aligned (this
 * makes the search algorithm much faster).
 *
 * The region is marked as set bits in the bitmap if a free one is
 * found.
 *
 * Returns either beginning of region or negative error
 */
int bitmap_find_free_region(unsigned long *bitmap, int bits, int order)
{
	unsigned long mask;
	int pages = 1 << order;
	int i;

	if(pages > BITS_PER_LONG)
		return -EINVAL;

	/* make a mask of the order */
	mask = (1ul << (pages - 1));
	mask += mask - 1;

	/* run up the bitmap pages bits at a time */
	for (i = 0; i < bits; i += pages) {
		int index = i/BITS_PER_LONG;
		int offset = i - (index * BITS_PER_LONG);
		if((bitmap[index] & (mask << offset)) == 0) {
			/* set region in bimap */
			bitmap[index] |= (mask << offset);
			return i;
		}
	}
	return -ENOMEM;
}
EXPORT_SYMBOL(bitmap_find_free_region);

/**
 *	bitmap_release_region - release allocated bitmap region
 *	@bitmap: a pointer to the bitmap
 *	@pos: the beginning of the region
 *	@order: the order of the bits to release (number is 1<<order)
 *
 * This is the complement to __bitmap_find_free_region and releases
 * the found region (by clearing it in the bitmap).
 */
void bitmap_release_region(unsigned long *bitmap, int pos, int order)
{
	int pages = 1 << order;
	unsigned long mask = (1ul << (pages - 1));
	int index = pos/BITS_PER_LONG;
	int offset = pos - (index * BITS_PER_LONG);
	mask += mask - 1;
	bitmap[index] &= ~(mask << offset);
}
EXPORT_SYMBOL(bitmap_release_region);

int bitmap_allocate_region(unsigned long *bitmap, int pos, int order)
{
	int pages = 1 << order;
	unsigned long mask = (1ul << (pages - 1));
	int index = pos/BITS_PER_LONG;
	int offset = pos - (index * BITS_PER_LONG);

	/* We don't do regions of pages > BITS_PER_LONG.  The
	 * algorithm would be a simple look for multiple zeros in the
	 * array, but there's no driver today that needs this.  If you
	 * trip this BUG(), you get to code it... */
	BUG_ON(pages > BITS_PER_LONG);
	mask += mask - 1;
	if (bitmap[index] & (mask << offset))
		return -EBUSY;
	bitmap[index] |= (mask << offset);
	return 0;
}
EXPORT_SYMBOL(bitmap_allocate_region);

#ifdef __BIG_ENDIAN

static void bitmap_long_to_byte(uint8_t *bp, const unsigned long *lp,
				unsigned int nbits)
{
	unsigned long l;
	int i, j, b;

	for (i = 0, b = 0; nbits > 0; i++, b += sizeof(l)) {
		l = lp[i];
		for (j = 0; (j < sizeof(l)) && (nbits > 0); j++) {
			bp[b+j] = l;
			l >>= 8;
			nbits -= 8;
		}
	}
	clamp_last_byte(bp, nbits);
}

static void bitmap_byte_to_long(unsigned long *lp, const uint8_t *bp,
				unsigned int nbits)
{
	unsigned long l;
	int i, j, b;

	for (i = 0, b = 0; nbits > 0; i++, b += sizeof(l)) {
		l = 0;
		for (j = 0; (j < sizeof(l)) && (nbits > 0); j++) {
			l |= (unsigned long)bp[b+j] << (j*8);
			nbits -= 8;
		}
		lp[i] = l;
	}
}

#elif defined(__LITTLE_ENDIAN)

static void bitmap_long_to_byte(uint8_t *bp, const unsigned long *lp,
				unsigned int nbits)
{
	memcpy(bp, lp, DIV_ROUND_UP(nbits, BITS_PER_BYTE));
	clamp_last_byte(bp, nbits);
}

static void bitmap_byte_to_long(unsigned long *lp, const uint8_t *bp,
				unsigned int nbits)
{
	/* We may need to pad the final longword with zeroes. */
	if (nbits & (BITS_PER_LONG-1))
		lp[BITS_TO_LONGS(nbits)-1] = 0;
	memcpy(lp, bp, DIV_ROUND_UP(nbits, BITS_PER_BYTE));
}

#endif

int bitmap_to_prtosctl_bitmap(struct prtosctl_bitmap *prtosctl_bitmap,
                            const unsigned long *bitmap, unsigned int nbits)
{
    unsigned int guest_bytes, copy_bytes, i;
    uint8_t zero = 0;
    int err = 0;
    unsigned int prtos_bytes = DIV_ROUND_UP(nbits, BITS_PER_BYTE);
    uint8_t *bytemap = xmalloc_array(uint8_t, prtos_bytes);

    if ( !bytemap )
        return -ENOMEM;

    guest_bytes = DIV_ROUND_UP(prtosctl_bitmap->nr_bits, BITS_PER_BYTE);
    copy_bytes  = min(guest_bytes, prtos_bytes);

    bitmap_long_to_byte(bytemap, bitmap, nbits);

    if ( copy_bytes &&
         copy_to_guest(prtosctl_bitmap->bitmap, bytemap, copy_bytes) )
        err = -EFAULT;

    xfree(bytemap);

    for ( i = copy_bytes; !err && i < guest_bytes; i++ )
        if ( copy_to_guest_offset(prtosctl_bitmap->bitmap, i, &zero, 1) )
            err = -EFAULT;

    return err;
}

int prtosctl_bitmap_to_bitmap(unsigned long *bitmap,
                            const struct prtosctl_bitmap *prtosctl_bitmap,
                            unsigned int nbits)
{
    unsigned int guest_bytes, copy_bytes;
    int err = 0;
    unsigned int prtos_bytes = DIV_ROUND_UP(nbits, BITS_PER_BYTE);
    uint8_t *bytemap = xzalloc_array(uint8_t, prtos_bytes);

    if ( !bytemap )
        return -ENOMEM;

    guest_bytes = DIV_ROUND_UP(prtosctl_bitmap->nr_bits, BITS_PER_BYTE);
    copy_bytes  = min(guest_bytes, prtos_bytes);

    if ( copy_bytes )
    {
        if ( copy_from_guest(bytemap, prtosctl_bitmap->bitmap, copy_bytes) )
            err = -EFAULT;
        if ( (prtosctl_bitmap->nr_bits & 7) && (guest_bytes == copy_bytes) )
            bytemap[guest_bytes - 1] &= ~(0xff << (prtosctl_bitmap->nr_bits & 7));
    }

    if ( !err )
        bitmap_byte_to_long(bitmap, bytemap, nbits);

    xfree(bytemap);

    return err;
}

int cpumask_to_prtosctl_bitmap(struct prtosctl_bitmap *prtosctl_cpumap,
                             const cpumask_t *cpumask)
{
    return bitmap_to_prtosctl_bitmap(prtosctl_cpumap, cpumask_bits(cpumask),
                                   nr_cpu_ids);
}

int prtosctl_bitmap_to_cpumask(cpumask_var_t *cpumask,
                             const struct prtosctl_bitmap *prtosctl_cpumap)
{
    int err = 0;

    if ( alloc_cpumask_var(cpumask) )
    {
        err = prtosctl_bitmap_to_bitmap(cpumask_bits(*cpumask), prtosctl_cpumap,
                                      nr_cpu_ids);
        /* In case of error, cleanup is up to us, as the caller won't care! */
        if ( err )
            free_cpumask_var(*cpumask);
    }
    else
        err = -ENOMEM;

    return err;
}

/* === END INLINED: bitmap.c === */
/* === BEGIN INLINED: bitops.c === */
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>
/*
 * Copyright (C) 2018 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <prtos_prtos_config.h>

#include <prtos_bitops.h>
#include <asm_system.h>

/*
 * The atomic bit operations pass the number of bit in a signed number
 * (not sure why). This has the drawback to increase the complexity of
 * the resulting assembly.
 *
 * To generate simpler code, the number of bit (nr) will be cast to
 * unsigned int.
 *
 * XXX: Rework the interface to use unsigned int.
 */

#define bitop(name, instr)                                                  \
static always_inline bool int_##name(int nr, volatile void *p, bool timeout,\
                                     unsigned int max_try)                  \
{                                                                           \
    volatile uint32_t *ptr = (volatile uint32_t *)p +                       \
                             BITOP_WORD((unsigned int)nr);                  \
    const uint32_t mask = BITOP_MASK((unsigned int)nr);                     \
    unsigned long res, tmp;                                                 \
                                                                            \
    do                                                                      \
    {                                                                       \
        asm volatile ("// " __stringify(name) "\n"                          \
        "   ldxr    %w2, %1\n"                                              \
        "   " __stringify(instr) "     %w2, %w2, %w3\n"                     \
        "   stxr    %w0, %w2, %1\n"                                         \
        : "=&r" (res), "+Q" (*ptr), "=&r" (tmp)                             \
        : "r" (mask));                                                      \
                                                                            \
        if ( !res )                                                         \
            break;                                                          \
    } while ( !timeout || ((--max_try) > 0) );                              \
                                                                            \
    return !res;                                                            \
}                                                                           \
                                                                            \
void name(int nr, volatile void *p)                                         \
{                                                                           \
    if ( !int_##name(nr, p, false, 0) )                                     \
        ASSERT_UNREACHABLE();                                               \
}                                                                           \
                                                                            \
bool name##_timeout(int nr, volatile void *p, unsigned int max_try)         \
{                                                                           \
    return int_##name(nr, p, true, max_try);                                \
}

#define testop(name, instr)                                                 \
static always_inline bool int_##name(int nr, volatile void *p, int *oldbit, \
                                     bool timeout, unsigned int max_try)    \
{                                                                           \
    volatile uint32_t *ptr = (volatile uint32_t *)p +                       \
                             BITOP_WORD((unsigned int)nr);                  \
    unsigned int bit = (unsigned int)nr % BITOP_BITS_PER_WORD;              \
    const uint32_t mask = BITOP_MASK(bit);                                  \
    unsigned long res, tmp;                                                 \
                                                                            \
    do                                                                      \
    {                                                                       \
        asm volatile ("// " __stringify(name) "\n"                          \
        "   ldxr    %w3, %2\n"                                              \
        "   lsr     %w1, %w3, %w5 // Save old value of bit\n"               \
        "   " __stringify(instr) "  %w3, %w3, %w4 // Toggle bit\n"          \
        "   stlxr   %w0, %w3, %2\n"                                         \
        : "=&r" (res), "=&r" (*oldbit), "+Q" (*ptr), "=&r" (tmp)            \
        : "r" (mask), "r" (bit)                                             \
        : "memory");                                                        \
                                                                            \
        if ( !res )                                                         \
            break;                                                          \
    } while ( !timeout || ((--max_try) > 0) );                              \
                                                                            \
    dmb(ish);                                                               \
                                                                            \
    *oldbit &= 1;                                                           \
                                                                            \
    return !res;                                                            \
}                                                                           \
                                                                            \
int name(int nr, volatile void *p)                                          \
{                                                                           \
    int oldbit;                                                             \
                                                                            \
    if ( !int_##name(nr, p, &oldbit, false, 0) )                            \
        ASSERT_UNREACHABLE();                                               \
                                                                            \
    return oldbit;                                                          \
}                                                                           \
                                                                            \
bool name##_timeout(int nr, volatile void *p,                               \
                    int *oldbit, unsigned int max_try)                      \
{                                                                           \
    return int_##name(nr, p, oldbit, true, max_try);                        \
}

bitop(change_bit, eor)
bitop(clear_bit, bic)
bitop(set_bit, orr)

testop(test_and_change_bit, eor)
testop(test_and_clear_bit, bic)
testop(test_and_set_bit, orr)

static always_inline bool int_clear_mask16(uint16_t mask, volatile uint16_t *p,
                                           bool timeout, unsigned int max_try)
{
    unsigned long res, tmp;

    do
    {
        asm volatile ("//  int_clear_mask16\n"
        "   ldxrh   %w2, %1\n"
        "   bic     %w2, %w2, %w3\n"
        "   stxrh   %w0, %w2, %1\n"
        : "=&r" (res), "+Q" (*p), "=&r" (tmp)
        : "r" (mask));

        if ( !res )
            break;
    } while ( !timeout || ((--max_try) > 0) );

    return !res;
}

void clear_mask16(uint16_t mask, volatile void *p)
{
    if ( !int_clear_mask16(mask, p, false, 0) )
        ASSERT_UNREACHABLE();
}

bool clear_mask16_timeout(uint16_t mask, volatile void *p,
                          unsigned int max_try)
{
    return int_clear_mask16(mask, p, true, max_try);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: bitops.c === */
/* === BEGIN INLINED: find-next-bit.c === */
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>
/* find_next_bit.c: fallback find next bit implementation
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <prtos_prtos_config.h>

#include <prtos_bitops.h>

#include <asm_byteorder.h>

#define __ffs(x) (ffsl(x) - 1)
#define ffz(x) __ffs(~(x))

#ifndef find_next_bit
/*
 * Find the next set bit in a memory region.
 */
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset)
{
	const unsigned long *p = addr + BIT_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG-1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
		tmp &= (~0UL << offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1)) {
		if ((tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;	/* Nope. */
found_middle:
	return result + __ffs(tmp);
}
EXPORT_SYMBOL(find_next_bit);
#endif

#ifndef find_next_zero_bit
/*
 * This implementation of find_{first,next}_zero_bit was stolen from
 * Linus' asm-alpha/bitops.h.
 */
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
				 unsigned long offset)
{
	const unsigned long *p = addr + BIT_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG-1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (BITS_PER_LONG - offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1)) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope. */
found_middle:
	return result + ffz(tmp);
}
EXPORT_SYMBOL(find_next_zero_bit);
#endif

#ifndef find_first_bit
/*
 * Find the first set bit in a memory region.
 */
unsigned long find_first_bit(const unsigned long *addr, unsigned long size)
{
	const unsigned long *p = addr;
	unsigned long result = 0;
	unsigned long tmp;

	while (size & ~(BITS_PER_LONG-1)) {
		if ((tmp = *(p++)))
			goto found;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;

	tmp = (*p) & (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;	/* Nope. */
found:
	return result + __ffs(tmp);
}
EXPORT_SYMBOL(find_first_bit);
#endif

#ifndef find_first_zero_bit
/*
 * Find the first cleared bit in a memory region.
 */
unsigned long find_first_zero_bit(const unsigned long *addr, unsigned long size)
{
	const unsigned long *p = addr;
	unsigned long result = 0;
	unsigned long tmp;

	while (size & ~(BITS_PER_LONG-1)) {
		if (~(tmp = *(p++)))
			goto found;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;

	tmp = (*p) | (~0UL << size);
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope. */
found:
	return result + ffz(tmp);
}
EXPORT_SYMBOL(find_first_zero_bit);
#endif

#ifdef __BIG_ENDIAN

/* include/linux/byteorder does not support "unsigned long" type */
static inline unsigned long ext2_swabp(const unsigned long * x)
{
#if BITS_PER_LONG == 64
	return (unsigned long) __swab64p((u64 *) x);
#elif BITS_PER_LONG == 32
	return (unsigned long) __swab32p((u32 *) x);
#else
#error BITS_PER_LONG not defined
#endif
}

/* include/linux/byteorder doesn't support "unsigned long" type */
static inline unsigned long ext2_swab(const unsigned long y)
{
#if BITS_PER_LONG == 64
	return (unsigned long) __swab64((u64) y);
#elif BITS_PER_LONG == 32
	return (unsigned long) __swab32((u32) y);
#else
#error BITS_PER_LONG not defined
#endif
}

#ifndef find_next_zero_bit_le
unsigned long find_next_zero_bit_le(const void *addr, unsigned
		long size, unsigned long offset)
{
	const unsigned long *p = addr;
	unsigned long result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	p += BIT_WORD(offset);
	size -= result;
	offset &= (BITS_PER_LONG - 1UL);
	if (offset) {
		tmp = ext2_swabp(p++);
		tmp |= (~0UL >> (BITS_PER_LONG - offset));
		if (size < BITS_PER_LONG)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}

	while (size & ~(BITS_PER_LONG - 1)) {
		if (~(tmp = *(p++)))
			goto found_middle_swap;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = ext2_swabp(p);
found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size; /* Nope. Skip ffz */
found_middle:
	return result + ffz(tmp);

found_middle_swap:
	return result + ffz(ext2_swab(tmp));
}
EXPORT_SYMBOL(find_next_zero_bit_le);
#endif

#ifndef find_next_bit_le
unsigned long find_next_bit_le(const void *addr, unsigned
		long size, unsigned long offset)
{
	const unsigned long *p = addr;
	unsigned long result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	p += BIT_WORD(offset);
	size -= result;
	offset &= (BITS_PER_LONG - 1UL);
	if (offset) {
		tmp = ext2_swabp(p++);
		tmp &= (~0UL << offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}

	while (size & ~(BITS_PER_LONG - 1)) {
		tmp = *(p++);
		if (tmp)
			goto found_middle_swap;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = ext2_swabp(p);
found_first:
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size; /* Nope. */
found_middle:
	return result + __ffs(tmp);

found_middle_swap:
	return result + __ffs(ext2_swab(tmp));
}
EXPORT_SYMBOL(find_next_bit_le);
#endif

#endif /* __BIG_ENDIAN */

/* === END INLINED: find-next-bit.c === */
/* === BEGIN INLINED: ctype.c === */
#include <prtos_prtos_config.h>
#include <prtos_ctype.h>

/* for ctype.h */
const unsigned char _ctype[] = {
    _C,_C,_C,_C,_C,_C,_C,_C,                        /* 0-7 */
    _C,_C|_S,_C|_S,_C|_S,_C|_S,_C|_S,_C,_C,         /* 8-15 */
    _C,_C,_C,_C,_C,_C,_C,_C,                        /* 16-23 */
    _C,_C,_C,_C,_C,_C,_C,_C,                        /* 24-31 */
    _S|_SP,_P,_P,_P,_P,_P,_P,_P,                    /* 32-39 */
    _P,_P,_P,_P,_P,_P,_P,_P,                        /* 40-47 */
    _D,_D,_D,_D,_D,_D,_D,_D,                        /* 48-55 */
    _D,_D,_P,_P,_P,_P,_P,_P,                        /* 56-63 */
    _P,_U|_X,_U|_X,_U|_X,_U|_X,_U|_X,_U|_X,_U,      /* 64-71 */
    _U,_U,_U,_U,_U,_U,_U,_U,                        /* 72-79 */
    _U,_U,_U,_U,_U,_U,_U,_U,                        /* 80-87 */
    _U,_U,_U,_P,_P,_P,_P,_P,                        /* 88-95 */
    _P,_L|_X,_L|_X,_L|_X,_L|_X,_L|_X,_L|_X,_L,      /* 96-103 */
    _L,_L,_L,_L,_L,_L,_L,_L,                        /* 104-111 */
    _L,_L,_L,_L,_L,_L,_L,_L,                        /* 112-119 */
    _L,_L,_L,_P,_P,_P,_P,_C,                        /* 120-127 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                /* 128-143 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                /* 144-159 */
    _S|_SP,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,   /* 160-175 */
    _P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,_P,       /* 176-191 */
    _U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,_U,       /* 192-207 */
    _U,_U,_U,_U,_U,_U,_U,_P,_U,_U,_U,_U,_U,_U,_U,_L,       /* 208-223 */
    _L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,_L,       /* 224-239 */
    _L,_L,_L,_L,_L,_L,_L,_P,_L,_L,_L,_L,_L,_L,_L,_L};      /* 240-255 */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: ctype.c === */
/* === BEGIN INLINED: radix-tree.c === */
#include <prtos_prtos_config.h>
/*
 * Copyright (C) 2001 Momchil Velikov
 * Portions Copyright (C) 2001 Christoph Hellwig
 * Copyright (C) 2005 SGI, Christoph Lameter
 * Copyright (C) 2006 Nick Piggin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <prtos_init.h>
#include <prtos_radix-tree.h>
#include <prtos_errno.h>

struct radix_tree_path {
	struct radix_tree_node *node;
	int offset;
};

#define RADIX_TREE_INDEX_BITS  (8 /* CHAR_BIT */ * sizeof(unsigned long))
#define RADIX_TREE_MAX_PATH (DIV_ROUND_UP(RADIX_TREE_INDEX_BITS, \
					  RADIX_TREE_MAP_SHIFT))

/*
 * The height_to_maxindex array needs to be one deeper than the maximum
 * path as height 0 holds only 1 entry.
 */
static unsigned long height_to_maxindex[RADIX_TREE_MAX_PATH + 1] __read_mostly;

static inline void *ptr_to_indirect(void *ptr)
{
	return (void *)((unsigned long)ptr | RADIX_TREE_INDIRECT_PTR);
}

static inline void *indirect_to_ptr(void *ptr)
{
	return (void *)((unsigned long)ptr & ~RADIX_TREE_INDIRECT_PTR);
}

struct rcu_node {
	struct radix_tree_node node;
	struct rcu_head rcu_head;
};

static struct radix_tree_node *cf_check rcu_node_alloc(void *arg)
{
	struct rcu_node *rcu_node = xmalloc(struct rcu_node);
	return rcu_node ? &rcu_node->node : NULL;
}

static void cf_check _rcu_node_free(struct rcu_head *head)
{
	struct rcu_node *rcu_node =
		container_of(head, struct rcu_node, rcu_head);
	xfree(rcu_node);
}

static void cf_check rcu_node_free(struct radix_tree_node *node, void *arg)
{
	struct rcu_node *rcu_node = container_of(node, struct rcu_node, node);
	call_rcu(&rcu_node->rcu_head, _rcu_node_free);
}

static struct radix_tree_node *radix_tree_node_alloc(
	struct radix_tree_root *root)
{
	struct radix_tree_node *ret;
	ret = root->node_alloc(root->node_alloc_free_arg);
	if (ret)
		memset(ret, 0, sizeof(*ret));
	return ret;
}

static void radix_tree_node_free(
	struct radix_tree_root *root, struct radix_tree_node *node)
{
	root->node_free(node, root->node_alloc_free_arg);
}

/*
 *	Return the maximum key which can be store into a
 *	radix tree with height HEIGHT.
 */
static inline unsigned long radix_tree_maxindex(unsigned int height)
{
	return height_to_maxindex[height];
}

/*
 *	Extend a radix tree so it can store key @index.
 */
static int radix_tree_extend(struct radix_tree_root *root, unsigned long index)
{
	struct radix_tree_node *node;
	unsigned int height;

	/* Figure out what the height should be.  */
	height = root->height + 1;
	while (index > radix_tree_maxindex(height))
		height++;

	if (root->rnode == NULL) {
		root->height = height;
		goto out;
	}

	do {
		unsigned int newheight;
		if (!(node = radix_tree_node_alloc(root)))
			return -ENOMEM;

		/* Increase the height.  */
		node->slots[0] = indirect_to_ptr(root->rnode);

		newheight = root->height+1;
		node->height = newheight;
		node->count = 1;
		node = ptr_to_indirect(node);
		rcu_assign_pointer(root->rnode, node);
		root->height = newheight;
	} while (height > root->height);
out:
	return 0;
}

/**
 *	radix_tree_insert    -    insert into a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *	@item:		item to insert
 *
 *	Insert an item into the radix tree at position @index.
 */
int radix_tree_insert(struct radix_tree_root *root,
			unsigned long index, void *item)
{
	struct radix_tree_node *node = NULL, *slot;
	unsigned int height, shift;
	int offset;
	int error;

	BUG_ON(radix_tree_is_indirect_ptr(item));

	/* Make sure the tree is high enough.  */
	if (index > radix_tree_maxindex(root->height)) {
		error = radix_tree_extend(root, index);
		if (error)
			return error;
	}

	slot = indirect_to_ptr(root->rnode);

	height = root->height;
	shift = (height-1) * RADIX_TREE_MAP_SHIFT;

	offset = 0;			/* uninitialised var warning */
	while (height > 0) {
		if (slot == NULL) {
			/* Have to add a child node.  */
			if (!(slot = radix_tree_node_alloc(root)))
				return -ENOMEM;
			slot->height = height;
			if (node) {
				rcu_assign_pointer(node->slots[offset], slot);
				node->count++;
			} else
				rcu_assign_pointer(root->rnode, ptr_to_indirect(slot));
		}

		/* Go a level down */
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		node = slot;
		slot = node->slots[offset];
		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	}

	if (slot != NULL)
		return -EEXIST;

	if (node) {
		node->count++;
		rcu_assign_pointer(node->slots[offset], item);
	} else {
		rcu_assign_pointer(root->rnode, item);
	}

	return 0;
}
EXPORT_SYMBOL(radix_tree_insert);

/*
 * is_slot == 1 : search for the slot.
 * is_slot == 0 : search for the node.
 */
static void *radix_tree_lookup_element(struct radix_tree_root *root,
				unsigned long index, int is_slot)
{
	unsigned int height, shift;
	struct radix_tree_node *node, **slot;

	node = rcu_dereference(root->rnode);
	if (node == NULL)
		return NULL;

	if (!radix_tree_is_indirect_ptr(node)) {
		if (index > 0)
			return NULL;
		return is_slot ? (void *)&root->rnode : node;
	}
	node = indirect_to_ptr(node);

	height = node->height;
	if (index > radix_tree_maxindex(height))
		return NULL;

	shift = (height-1) * RADIX_TREE_MAP_SHIFT;

	do {
		slot = (struct radix_tree_node **)
			(node->slots + ((index>>shift) & RADIX_TREE_MAP_MASK));
		node = rcu_dereference(*slot);
		if (node == NULL)
			return NULL;

		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	} while (height > 0);

	return is_slot ? (void *)slot : indirect_to_ptr(node);
}

/**
 *	radix_tree_lookup_slot    -    lookup a slot in a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Returns:  the slot corresponding to the position @index in the
 *	radix tree @root. This is useful for update-if-exists operations.
 *
 *	This function can be called under rcu_read_lock iff the slot is not
 *	modified by radix_tree_replace_slot, otherwise it must be called
 *	exclusive from other writers. Any dereference of the slot must be done
 *	using radix_tree_deref_slot.
 */
void **radix_tree_lookup_slot(struct radix_tree_root *root, unsigned long index)
{
	return (void **)radix_tree_lookup_element(root, index, 1);
}
EXPORT_SYMBOL(radix_tree_lookup_slot);

/**
 *	radix_tree_lookup    -    perform lookup operation on a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Lookup the item at the position @index in the radix tree @root.
 *
 *	This function can be called under rcu_read_lock, however the caller
 *	must manage lifetimes of leaf nodes (eg. RCU may also be used to free
 *	them safely). No RCU barriers are required to access or modify the
 *	returned item, however.
 */
void *radix_tree_lookup(struct radix_tree_root *root, unsigned long index)
{
	return radix_tree_lookup_element(root, index, 0);
}
EXPORT_SYMBOL(radix_tree_lookup);

/**
 *	radix_tree_next_hole    -    find the next hole (not-present entry)
 *	@root:		tree root
 *	@index:		index key
 *	@max_scan:	maximum range to search
 *
 *	Search the set [index, min(index+max_scan-1, MAX_INDEX)] for the lowest
 *	indexed hole.
 *
 *	Returns: the index of the hole if found, otherwise returns an index
 *	outside of the set specified (in which case 'return - index >= max_scan'
 *	will be true). In rare cases of index wrap-around, 0 will be returned.
 *
 *	radix_tree_next_hole may be called under rcu_read_lock. However, like
 *	radix_tree_gang_lookup, this will not atomically search a snapshot of
 *	the tree at a single point in time. For example, if a hole is created
 *	at index 5, then subsequently a hole is created at index 10,
 *	radix_tree_next_hole covering both indexes may return 10 if called
 *	under rcu_read_lock.
 */
unsigned long radix_tree_next_hole(struct radix_tree_root *root,
				unsigned long index, unsigned long max_scan)
{
	unsigned long i;

	for (i = 0; i < max_scan; i++) {
		if (!radix_tree_lookup(root, index))
			break;
		index++;
		if (index == 0)
			break;
	}

	return index;
}
EXPORT_SYMBOL(radix_tree_next_hole);

/**
 *	radix_tree_prev_hole    -    find the prev hole (not-present entry)
 *	@root:		tree root
 *	@index:		index key
 *	@max_scan:	maximum range to search
 *
 *	Search backwards in the range [max(index-max_scan+1, 0), index]
 *	for the first hole.
 *
 *	Returns: the index of the hole if found, otherwise returns an index
 *	outside of the set specified (in which case 'index - return >= max_scan'
 *	will be true). In rare cases of wrap-around, ULONG_MAX will be returned.
 *
 *	radix_tree_next_hole may be called under rcu_read_lock. However, like
 *	radix_tree_gang_lookup, this will not atomically search a snapshot of
 *	the tree at a single point in time. For example, if a hole is created
 *	at index 10, then subsequently a hole is created at index 5,
 *	radix_tree_prev_hole covering both indexes may return 5 if called under
 *	rcu_read_lock.
 */
unsigned long radix_tree_prev_hole(struct radix_tree_root *root,
				   unsigned long index, unsigned long max_scan)
{
	unsigned long i;

	for (i = 0; i < max_scan; i++) {
		if (!radix_tree_lookup(root, index))
			break;
		index--;
		if (index == ULONG_MAX)
			break;
	}

	return index;
}
EXPORT_SYMBOL(radix_tree_prev_hole);

static unsigned int
__lookup(struct radix_tree_node *slot, void ***results, unsigned long index,
	unsigned int max_items, unsigned long *next_index)
{
	unsigned int nr_found = 0;
	unsigned int shift, height;
	unsigned long i;

	height = slot->height;
	if (height == 0)
		goto out;
	shift = (height-1) * RADIX_TREE_MAP_SHIFT;

	for ( ; height > 1; height--) {
		i = (index >> shift) & RADIX_TREE_MAP_MASK;
		for (;;) {
			if (slot->slots[i] != NULL)
				break;
			index &= ~((1UL << shift) - 1);
			index += 1UL << shift;
			if (index == 0)
				goto out;	/* 32-bit wraparound */
			i++;
			if (i == RADIX_TREE_MAP_SIZE)
				goto out;
		}

		shift -= RADIX_TREE_MAP_SHIFT;
		slot = rcu_dereference(slot->slots[i]);
		if (slot == NULL)
			goto out;
	}

	/* Bottom level: grab some items */
	for (i = index & RADIX_TREE_MAP_MASK; i < RADIX_TREE_MAP_SIZE; i++) {
		index++;
		if (slot->slots[i]) {
			results[nr_found++] = &(slot->slots[i]);
			if (nr_found == max_items)
				goto out;
		}
	}
out:
	*next_index = index;
	return nr_found;
}

/**
 *	radix_tree_gang_lookup - perform multiple lookup on a radix tree
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *
 *	Performs an index-ascending scan of the tree for present items.  Places
 *	them at *@results and returns the number of items which were placed at
 *	*@results.
 *
 *	The implementation is naive.
 *
 *	Like radix_tree_lookup, radix_tree_gang_lookup may be called under
 *	rcu_read_lock. In this case, rather than the returned results being
 *	an atomic snapshot of the tree at a single point in time, the semantics
 *	of an RCU protected gang lookup are as though multiple radix_tree_lookups
 *	have been issued in individual locks, and results stored in 'results'.
 */
unsigned int
radix_tree_gang_lookup(struct radix_tree_root *root, void **results,
			unsigned long first_index, unsigned int max_items)
{
	unsigned long max_index;
	struct radix_tree_node *node;
	unsigned long cur_index = first_index;
	unsigned int ret;

	node = rcu_dereference(root->rnode);
	if (!node)
		return 0;

	if (!radix_tree_is_indirect_ptr(node)) {
		if (first_index > 0)
			return 0;
		results[0] = node;
		return 1;
	}
	node = indirect_to_ptr(node);

	max_index = radix_tree_maxindex(node->height);

	ret = 0;
	while (ret < max_items) {
		unsigned int nr_found, slots_found, i;
		unsigned long next_index;	/* Index of next search */

		if (cur_index > max_index)
			break;
		slots_found = __lookup(node, (void ***)results + ret, cur_index,
					max_items - ret, &next_index);
		nr_found = 0;
		for (i = 0; i < slots_found; i++) {
			struct radix_tree_node *slot;
			slot = *(((void ***)results)[ret + i]);
			if (!slot)
				continue;
			results[ret + nr_found] =
				indirect_to_ptr(rcu_dereference(slot));
			nr_found++;
		}
		ret += nr_found;
		if (next_index == 0)
			break;
		cur_index = next_index;
	}

	return ret;
}
EXPORT_SYMBOL(radix_tree_gang_lookup);

/**
 *	radix_tree_gang_lookup_slot - perform multiple slot lookup on radix tree
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *
 *	Performs an index-ascending scan of the tree for present items.  Places
 *	their slots at *@results and returns the number of items which were
 *	placed at *@results.
 *
 *	The implementation is naive.
 *
 *	Like radix_tree_gang_lookup as far as RCU and locking goes. Slots must
 *	be dereferenced with radix_tree_deref_slot, and if using only RCU
 *	protection, radix_tree_deref_slot may fail requiring a retry.
 */
unsigned int
radix_tree_gang_lookup_slot(struct radix_tree_root *root, void ***results,
			unsigned long first_index, unsigned int max_items)
{
	unsigned long max_index;
	struct radix_tree_node *node;
	unsigned long cur_index = first_index;
	unsigned int ret;

	node = rcu_dereference(root->rnode);
	if (!node)
		return 0;

	if (!radix_tree_is_indirect_ptr(node)) {
		if (first_index > 0)
			return 0;
		results[0] = (void **)&root->rnode;
		return 1;
	}
	node = indirect_to_ptr(node);

	max_index = radix_tree_maxindex(node->height);

	ret = 0;
	while (ret < max_items) {
		unsigned int slots_found;
		unsigned long next_index;	/* Index of next search */

		if (cur_index > max_index)
			break;
		slots_found = __lookup(node, results + ret, cur_index,
					max_items - ret, &next_index);
		ret += slots_found;
		if (next_index == 0)
			break;
		cur_index = next_index;
	}

	return ret;
}
EXPORT_SYMBOL(radix_tree_gang_lookup_slot);

/**
 *	radix_tree_shrink    -    shrink height of a radix tree to minimal
 *	@root		radix tree root
 */
static inline void radix_tree_shrink(struct radix_tree_root *root)
{
	/* try to shrink tree height */
	while (root->height > 0) {
		struct radix_tree_node *to_free = root->rnode;
		void *newptr;

		BUG_ON(!radix_tree_is_indirect_ptr(to_free));
		to_free = indirect_to_ptr(to_free);

		/*
		 * The candidate node has more than one child, or its child
		 * is not at the leftmost slot, we cannot shrink.
		 */
		if (to_free->count != 1)
			break;
		if (!to_free->slots[0])
			break;

		/*
		 * We don't need rcu_assign_pointer(), since we are simply
		 * moving the node from one part of the tree to another: if it
		 * was safe to dereference the old pointer to it
		 * (to_free->slots[0]), it will be safe to dereference the new
		 * one (root->rnode) as far as dependent read barriers go.
		 */
		newptr = to_free->slots[0];
		if (root->height > 1)
			newptr = ptr_to_indirect(newptr);
		root->rnode = newptr;
		root->height--;

		/*
		 * We have a dilemma here. The node's slot[0] must not be
		 * NULLed in case there are concurrent lookups expecting to
		 * find the item. However if this was a bottom-level node,
		 * then it may be subject to the slot pointer being visible
		 * to callers dereferencing it. If item corresponding to
		 * slot[0] is subsequently deleted, these callers would expect
		 * their slot to become empty sooner or later.
		 *
		 * For example, lockless pagecache will look up a slot, deref
		 * the page pointer, and if the page is 0 refcount it means it
		 * was concurrently deleted from pagecache so try the deref
		 * again. Fortunately there is already a requirement for logic
		 * to retry the entire slot lookup -- the indirect pointer
		 * problem (replacing direct root node with an indirect pointer
		 * also results in a stale slot). So tag the slot as indirect
		 * to force callers to retry.
		 */
		if (root->height == 0)
			*((unsigned long *)&to_free->slots[0]) |=
						RADIX_TREE_INDIRECT_PTR;

		radix_tree_node_free(root, to_free);
	}
}

/**
 *	radix_tree_delete    -    delete an item from a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Remove the item at @index from the radix tree rooted at @root.
 *
 *	Returns the address of the deleted item, or NULL if it was not present.
 */
void *radix_tree_delete(struct radix_tree_root *root, unsigned long index)
{
	/*
	 * The radix tree path needs to be one longer than the maximum path
	 * since the "list" is null terminated.
	 */
	struct radix_tree_path path[RADIX_TREE_MAX_PATH + 1], *pathp = path;
	struct radix_tree_node *slot = NULL;
	struct radix_tree_node *to_free;
	unsigned int height, shift;
	int offset;

	height = root->height;
	if (index > radix_tree_maxindex(height))
		goto out;

	slot = root->rnode;
	if (height == 0) {
		root->rnode = NULL;
		goto out;
	}
	slot = indirect_to_ptr(slot);

	shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
	pathp->node = NULL;

	do {
		if (slot == NULL)
			goto out;

		pathp++;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		pathp->offset = offset;
		pathp->node = slot;
		slot = slot->slots[offset];
		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	} while (height > 0);

	if (slot == NULL)
		goto out;

	to_free = NULL;
	/* Now free the nodes we do not need anymore */
	while (pathp->node) {
		pathp->node->slots[pathp->offset] = NULL;
		pathp->node->count--;
		/*
		 * Queue the node for deferred freeing after the
		 * last reference to it disappears (set NULL, above).
		 */
		if (to_free)
			radix_tree_node_free(root, to_free);

		if (pathp->node->count) {
			if (pathp->node == indirect_to_ptr(root->rnode))
				radix_tree_shrink(root);
			goto out;
		}

		/* Node with zero slots in use so free it */
		to_free = pathp->node;
		pathp--;

	}
	root->height = 0;
	root->rnode = NULL;
	if (to_free)
		radix_tree_node_free(root, to_free);

out:
	return slot;
}
EXPORT_SYMBOL(radix_tree_delete);

static void
radix_tree_node_destroy(
	struct radix_tree_root *root, struct radix_tree_node *node,
	void (*slot_free)(void *))
{
	int i;

	for (i = 0; i < RADIX_TREE_MAP_SIZE; i++) {
		struct radix_tree_node *slot = node->slots[i];
		BUG_ON(radix_tree_is_indirect_ptr(slot));
		if (slot == NULL)
			continue;
		if (node->height == 1) {
			if (slot_free)
				slot_free(slot);
		} else {
			radix_tree_node_destroy(root, slot, slot_free);
		}
	}

	radix_tree_node_free(root, node);
}

void radix_tree_destroy(
	struct radix_tree_root *root,
	void (*slot_free)(void *))
{
	struct radix_tree_node *node = root->rnode;
	if (node == NULL)
		return;
	if (!radix_tree_is_indirect_ptr(node)) {
		if (slot_free)
			slot_free(node);
	} else {
		node = indirect_to_ptr(node);
		radix_tree_node_destroy(root, node, slot_free);
	}
	radix_tree_init(root);
}

void radix_tree_init(struct radix_tree_root *root)
{
	memset(root, 0, sizeof(*root));
	root->node_alloc = rcu_node_alloc;
	root->node_free = rcu_node_free;
}


static __init unsigned long __maxindex(unsigned int height)
{
	unsigned int width = height * RADIX_TREE_MAP_SHIFT;
	int shift = RADIX_TREE_INDEX_BITS - width;

	if (shift < 0)
		return ~0UL;
	if (shift >= BITS_PER_LONG)
		return 0UL;
	return ~0UL >> shift;
}

static int __init cf_check radix_tree_init_maxindex(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(height_to_maxindex); i++)
		height_to_maxindex[i] = __maxindex(i);

	return 0;
}
/* pre-SMP just so it runs before 'normal' initcalls */
presmp_initcall(radix_tree_init_maxindex);

/* === END INLINED: radix-tree.c === */
/* === BEGIN INLINED: rbtree.c === */
#include <prtos_prtos_config.h>
/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  (C) 2002  David Woodhouse <dwmw2@infradead.org>
  (C) 2012  Michel Lespinasse <walken@google.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; If not, see <http://www.gnu.org/licenses/>.

  linux/lib/rbtree.c
*/

#include <prtos_types.h>
#include <prtos_rbtree.h>

/*
 * red-black trees properties:  http://en.wikipedia.org/wiki/Rbtree
 *
 *  1) A node is either red or black
 *  2) The root is black
 *  3) All leaves (NULL) are black
 *  4) Both children of every red node are black
 *  5) Every simple path from root to leaves contains the same number
 *     of black nodes.
 *
 *  4 and 5 give the O(log n) guarantee, since 4 implies you cannot have two
 *  consecutive red nodes in a path and every red node is therefore followed by
 *  a black. So if B is the number of black nodes on every simple path (as per
 *  5), then the longest possible path due to 4 is 2B.
 *
 *  We shall indicate color with case, where black nodes are uppercase and red
 *  nodes will be lowercase. Unknown color nodes shall be drawn as red within
 *  parentheses and have some accompanying text comment.
 */

#define		RB_RED		0
#define		RB_BLACK	1

#define __rb_parent(pc)    ((struct rb_node *)((pc) & ~3))

#define __rb_color(pc)     ((pc) & 1)
#define __rb_is_black(pc)  __rb_color(pc)
#define __rb_is_red(pc)    (!__rb_color(pc))
#define rb_color(rb)       __rb_color((rb)->__rb_parent_color)
#define rb_is_red(rb)      __rb_is_red((rb)->__rb_parent_color)
#define rb_is_black(rb)    __rb_is_black((rb)->__rb_parent_color)

static inline void rb_set_black(struct rb_node *rb)
{
	rb->__rb_parent_color |= RB_BLACK;
}

static inline void rb_set_parent(struct rb_node *rb, struct rb_node *p)
{
	rb->__rb_parent_color = rb_color(rb) | (unsigned long)p;
}

static inline void rb_set_parent_color(struct rb_node *rb,
				      struct rb_node *p, int color)
{
	rb->__rb_parent_color = (unsigned long)p | color;
}

static inline struct rb_node *rb_red_parent(struct rb_node *red)
{
	return (struct rb_node *)red->__rb_parent_color;
}

static inline void
__rb_change_child(struct rb_node *old, struct rb_node *new,
		  struct rb_node *parent, struct rb_root *root)
{
	if (parent) {
		if (parent->rb_left == old)
			parent->rb_left = new;
		else
			parent->rb_right = new;
	} else
		root->rb_node = new;
}

/*
 * Helper function for rotations:
 * - old's parent and color get assigned to new
 * - old gets assigned new as a parent and 'color' as a color.
 */
static inline void
__rb_rotate_set_parents(struct rb_node *old, struct rb_node *new,
			struct rb_root *root, int color)
{
	struct rb_node *parent = rb_parent(old);
	new->__rb_parent_color = old->__rb_parent_color;
	rb_set_parent_color(old, new, color);
	__rb_change_child(old, new, parent, root);
}


/* === END INLINED: rbtree.c === */
/* rangeset.c compiled separately - static merge() conflicts with list-sort.c */
/* === BEGIN INLINED: random.c === */
#include <prtos_prtos_config.h>
#include <prtos_cache.h>
#include <prtos_init.h>
#include <prtos_percpu.h>
#include <prtos_random.h>
#include <prtos_time.h>
#include <asm_generic_random.h>

static DEFINE_PER_CPU(unsigned int, seed);
unsigned int __read_mostly boot_random;

unsigned int get_random(void)
{
    unsigned int next = this_cpu(seed), val = arch_get_random();

    if ( unlikely(!next) )
        next = val ?: NOW();

    if ( !val )
    {
        unsigned int i;

        for ( i = 0; i < sizeof(val) * 8; i += 11 )
        {
            next = next * 1103515245 + 12345;
            val |= ((next >> 16) & 0x7ff) << i;
        }
    }

    this_cpu(seed) = next;

    return val;
}

static int __init cf_check init_boot_random(void)
{
    boot_random = get_random();
    return 0;
}
__initcall(init_boot_random);

/* === END INLINED: random.c === */
/* === BEGIN INLINED: parse-size.c === */
#include <prtos_prtos_config.h>
#include <prtos_lib.h>

unsigned long long parse_size_and_unit(const char *s, const char **ps)
{
    unsigned long long ret;
    const char *s1;

    ret = simple_strtoull(s, &s1, 0);

    switch ( *s1 )
    {
    case 'T': case 't':
        ret <<= 10;
        /* fallthrough */
    case 'G': case 'g':
        ret <<= 10;
        /* fallthrough */
    case 'M': case 'm':
        ret <<= 10;
        /* fallthrough */
    case 'K': case 'k':
        ret <<= 10;
        /* fallthrough */
    case 'B': case 'b':
        s1++;
        break;
    case '%':
        if ( ps )
            break;
        /* fallthrough */
    default:
        ret <<= 10; /* default to kB */
        break;
    }

    if ( ps != NULL )
        *ps = s1;

    return ret;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: parse-size.c === */
/* === BEGIN INLINED: memchr_inv.c === */
#include <prtos_prtos_config.h>
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <prtos_string.h>

/**
 * memchr_inv - Find an unmatching character in an area of memory.
 * @s: The memory area
 * @c: The byte that is expected
 * @n: The size of the area.
 *
 * returns the address of the first occurrence of a character other than @c,
 * or %NULL if the whole buffer contains just @c.
 */
void *memchr_inv(const void *s, int c, size_t n)
{
	const unsigned char *p = s;

	while (n--)
		if ((unsigned char)c != *p++)
			return (void *)(p - 1);

	return NULL;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 */

/* === END INLINED: memchr_inv.c === */
/* === BEGIN INLINED: muldiv64.c === */
#include <prtos_prtos_config.h>
#include <prtos_lib.h>

/* Compute with 96 bit intermediate result: (a*b)/c */
uint64_t muldiv64(uint64_t a, uint32_t b, uint32_t c)
{
#ifdef CONFIG_X86
    asm ( "mulq %1; divq %2" : "+a" (a)
                             : "rm" ((uint64_t)b), "rm" ((uint64_t)c)
                             : "rdx" );

    return a;
#else
    union {
        uint64_t ll;
        struct {
#ifdef WORDS_BIGENDIAN
            uint32_t high, low;
#else
            uint32_t low, high;
#endif
        } l;
    } u, res;
    uint64_t rl, rh;

    u.ll = a;
    rl = (uint64_t)u.l.low * (uint64_t)b;
    rh = (uint64_t)u.l.high * (uint64_t)b;
    rh += (rl >> 32);
    res.l.high = rh / c;
    res.l.low = (((rh % c) << 32) + (uint32_t)rl) / c;

    return res.ll;
#endif
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: muldiv64.c === */
/* === BEGIN INLINED: strcasecmp.c === */
#include <prtos_prtos_config.h>
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <prtos_string.h>
#include <prtos_ctype.h>

int (strcasecmp)(const char *s1, const char *s2)
{
    int c1, c2;

    do
    {
        c1 = tolower(*s1++);
        c2 = tolower(*s2++);
    } while ( c1 == c2 && c1 != 0 );

    return c1 - c2;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 */

/* === END INLINED: strcasecmp.c === */
/* === BEGIN INLINED: strlcat.c === */
#include <prtos_prtos_config.h>
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <prtos_string.h>

/**
 * strlcat - Append a %NUL terminated string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @size: size of destination buffer
 *
 * Compatible with *BSD: the result is always a valid
 * NUL-terminated string that fits in the buffer (unless,
 * of course, the buffer size is zero).
 */
size_t strlcat(char *dest, const char *src, size_t size)
{
	size_t slen = strlen(src);
	size_t dlen = strnlen(dest, size);
	char *p = dest + dlen;

	while ((p - dest) < size)
		if ((*p++ = *src++) == '\0')
			break;

	if (dlen < size)
		*(p-1) = '\0';

	return slen + dlen;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 */

/* === END INLINED: strlcat.c === */
/* === BEGIN INLINED: strlcpy.c === */
#include <prtos_prtos_config.h>
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <prtos_string.h>

/**
 * strlcpy - Copy a %NUL terminated string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @size: size of destination buffer
 *
 * Compatible with *BSD: the result is always a valid
 * NUL-terminated string that fits in the buffer (unless,
 * of course, the buffer size is zero). It does not pad
 * out the result like strncpy() does.
 */
size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size-1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 */

/* === END INLINED: strlcpy.c === */
/* === BEGIN INLINED: strncasecmp.c === */
#include <prtos_prtos_config.h>
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <prtos_string.h>
#include <prtos_ctype.h>

/**
 * strncasecmp - Case insensitive, length-limited string comparison
 * @s1: One string
 * @s2: The other string
 * @len: the maximum number of characters to compare
 */
int (strncasecmp)(const char *s1, const char *s2, size_t len)
{
	/* Yes, Virginia, it had better be unsigned */
	unsigned char c1, c2;

	c1 = 0;	c2 = 0;
	if (len) {
		do {
			c1 = *s1; c2 = *s2;
			s1++; s2++;
			if (!c1)
				break;
			if (!c2)
				break;
			if (c1 == c2)
				continue;
			c1 = tolower(c1);
			c2 = tolower(c2);
			if (c1 != c2)
				break;
		} while (--len);
	}
	return (int)c1 - (int)c2;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 */

/* === END INLINED: strncasecmp.c === */
/* === BEGIN INLINED: strpbrk.c === */
#include <prtos_prtos_config.h>
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <prtos_string.h>

/**
 * strpbrk - Find the first occurrence of a set of characters
 * @cs: The string to be searched
 * @ct: The characters to search for
 */
char *strpbrk(const char * cs,const char * ct)
{
	const char *sc1,*sc2;

	for( sc1 = cs; *sc1 != '\0'; ++sc1) {
		for( sc2 = ct; *sc2 != '\0'; ++sc2) {
			if (*sc1 == *sc2)
				return (char *) sc1;
		}
	}
	return NULL;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 */

/* === END INLINED: strpbrk.c === */
/* === BEGIN INLINED: strtol.c === */
#include <prtos_prtos_config.h>
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <prtos_lib.h>

/**
 * simple_strtol - convert a string to a signed long
 * @cp: The start of the string
 * @endp: A pointer to the end of the parsed string will be placed here
 * @base: The number base to use
 */
long simple_strtol(const char *cp, const char **endp, unsigned int base)
{
    if ( *cp == '-' )
        return -simple_strtoul(cp + 1, endp, base);
    return simple_strtoul(cp, endp, base);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: strtol.c === */
/* === BEGIN INLINED: strtoll.c === */
#include <prtos_prtos_config.h>
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <prtos_lib.h>

/**
 * simple_strtoll - convert a string to a signed long long
 * @cp: The start of the string
 * @endp: A pointer to the end of the parsed string will be placed here
 * @base: The number base to use
 */
long long simple_strtoll(const char *cp, const char **endp, unsigned int base)
{
    if ( *cp == '-' )
        return -simple_strtoull(cp + 1, endp, base);
    return simple_strtoull(cp, endp, base);
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: strtoll.c === */
/* === BEGIN INLINED: strtoul.c === */
#include <prtos_prtos_config.h>
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <prtos_ctype.h>
#include <prtos_lib.h>

/**
 * simple_strtoul - convert a string to an unsigned long
 * @cp: The start of the string
 * @endp: A pointer to the end of the parsed string will be placed here
 * @base: The number base to use
 */
unsigned long simple_strtoul(
    const char *cp, const char **endp, unsigned int base)
{
    unsigned long result = 0, value;

    if ( !base )
    {
        base = 10;
        if ( *cp == '0' )
        {
            base = 8;
            cp++;
            if ( (toupper(*cp) == 'X') && isxdigit(cp[1]) )
            {
                cp++;
                base = 16;
            }
        }
    }
    else if ( base == 16 )
    {
        if ( cp[0] == '0' && toupper(cp[1]) == 'X' )
            cp += 2;
    }

    while ( isxdigit(*cp) &&
            (value = isdigit(*cp) ? *cp - '0'
                                  : toupper(*cp) - 'A' + 10) < base )
    {
        result = result * base + value;
        cp++;
    }

    if ( endp )
        *endp = cp;

    return result;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: strtoul.c === */
/* === BEGIN INLINED: strtoull.c === */
#include <prtos_prtos_config.h>
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <prtos_ctype.h>
#include <prtos_lib.h>

/**
 * simple_strtoull - convert a string to an unsigned long long
 * @cp: The start of the string
 * @endp: A pointer to the end of the parsed string will be placed here
 * @base: The number base to use
 */
unsigned long long simple_strtoull(
    const char *cp, const char **endp, unsigned int base)
{
    unsigned long long result = 0, value;

    if ( !base )
    {
        base = 10;
        if ( *cp == '0' )
        {
            base = 8;
            cp++;
            if ( (toupper(*cp) == 'X') && isxdigit(cp[1]) )
            {
                cp++;
                base = 16;
            }
        }
    }
    else if ( base == 16 )
    {
        if ( cp[0] == '0' && toupper(cp[1]) == 'X' )
            cp += 2;
    }

    while ( isxdigit(*cp) &&
            (value = isdigit(*cp) ? *cp - '0'
                                  : (islower(*cp) ? toupper(*cp)
                                                  : *cp) - 'A' + 10) < base )
    {
        result = result * base + value;
        cp++;
    }

    if ( endp )
        *endp = cp;

    return result;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: strtoull.c === */
/* === BEGIN INLINED: gunzip.c === */
#include <prtos_prtos_config.h>
#include <prtos_errno.h>
#include <prtos_gunzip.h>
#include <prtos_init.h>
#include <prtos_lib.h>
#include <prtos_mm.h>

#define WSIZE           0x80000000U

struct gunzip_state {
    unsigned char *window;

    /* window position */
    unsigned int wp;

    unsigned char *inbuf;
    unsigned int insize;
    /* Index of next byte to be processed in inbuf: */
    unsigned int inptr;

    unsigned long bytes_out;

    unsigned long bb;      /* bit buffer */
    unsigned int  bk;      /* bits in bit buffer */

    uint32_t crc_32_tab[256];
    uint32_t crc;
};

#define malloc(a)       xmalloc_bytes(a)
#define free(a)         xfree(a)
#define memzero(s, n)   memset((s), 0, (n))

typedef unsigned char   uch;
typedef unsigned short  ush;
typedef unsigned long   ulg;

/* Diagnostic functions */
#ifdef DEBUG
#  define Assert(cond, msg) do { if (!(cond)) error(msg); } while (0)
#  define Trace(x)      do { fprintf x; } while (0)
#  define Tracev(x)     do { if (verbose) fprintf x ; } while (0)
#  define Tracevv(x)    do { if (verbose > 1) fprintf x ; } while (0)
#  define Tracec(c, x)  do { if (verbose && (c)) fprintf x ; } while (0)
#  define Tracecv(c, x) do { if (verbose > 1 && (c)) fprintf x ; } while (0)
#else
#  define Assert(cond, msg)
#  define Trace(x)
#  define Tracev(x)
#  define Tracevv(x)
#  define Tracec(c, x)
#  define Tracecv(c, x)
#endif

static void flush_window(struct gunzip_state *s);

static __init void error(const char *x)
{
    panic("%s\n", x);
}

static __init int get_byte(struct gunzip_state *s)
{
    if ( s->inptr >= s->insize )
    {
        error("ran out of input data");
        return -1;
    }

    return s->inbuf[s->inptr++];
}

/* === BEGIN INLINED: inflate.c === */
#define DEBG(x)
#define DEBG1(x)
/*
 * inflate.c -- Not copyrighted 1992 by Mark Adler
 * version c10p1, 10 January 1993
 */

/*
 * Adapted for booting Linux by Hannu Savolainen 1993
 * based on gzip-1.0.3
 *
 * Nicolas Pitre <nico@cam.org>, 1999/04/14 :
 *   Little mods for all variable to reside either into rodata or bss segments
 *   by marking constant variables with 'const' and initializing all the others
 *   at run-time only.  This allows for the kernel uncompressor to run
 *   directly from Flash or ROM memory on embedded systems.
 */

/*
 * Inflate deflated (PKZIP's method 8 compressed) data.  The compression
 * method searches for as much of the current string of bytes (up to a
 * length of 258) in the previous 32 K bytes.  If it doesn't find any
 * matches (of at least length 3), it codes the next byte.  Otherwise, it
 * codes the length of the matched string and its distance backwards from
 * the current position.  There is a single Huffman code that codes both
 * single bytes (called "literals") and match lengths.  A second Huffman
 * code codes the distance information, which follows a length code.  Each
 * length or distance code actually represents a base value and a number
 * of "extra" (sometimes zero) bits to get to add to the base value.  At
 * the end of each deflated block is a special end-of-block (EOB) literal/
 * length code.  The decoding process is basically: get a literal/length
 * code; if EOB then done; if a literal, emit the decoded byte; if a
 * length then get the distance and emit the referred-to bytes from the
 * sliding window of previously emitted data.
 *
 * There are (currently) three kinds of inflate blocks: stored, fixed, and
 * dynamic.  The compressor deals with some chunk of data at a time, and
 * decides which method to use on a chunk-by-chunk basis.  A chunk might
 * typically be 32 K or 64 K.  If the chunk is incompressible, then the
 * "stored" method is used.  In this case, the bytes are simply stored as
 * is, eight bits per byte, with none of the above coding.  The bytes are
 * preceded by a count, since there is no longer an EOB code.
 *
 * If the data is compressible, then either the fixed or dynamic methods
 * are used.  In the dynamic method, the compressed data is preceded by
 * an encoding of the literal/length and distance Huffman codes that are
 * to be used to decode this block.  The representation is itself Huffman
 * coded, and so is preceded by a description of that code.  These code
 * descriptions take up a little space, and so for small blocks, there is
 * a predefined set of codes, called the fixed codes.  The fixed method is
 * used if the block codes up smaller that way (usually for quite small
 * chunks), otherwise the dynamic method is used.  In the latter case, the
 * codes are customized to the probabilities in the current block, and so
 * can code it much better than the pre-determined fixed codes.
 *
 * The Huffman codes themselves are decoded using a multi-level table
 * lookup, in order to maximize the speed of decoding plus the speed of
 * building the decoding tables.  See the comments below that precede the
 * lbits and dbits tuning parameters.
 */

/*
 * Notes beyond the 1.93a appnote.txt:
 *
 *  1. Distance pointers never point before the beginning of the output
 *     stream.
 *  2. Distance pointers can point back across blocks, up to 32k away.
 *  3. There is an implied maximum of 7 bits for the bit length table and
 *     15 bits for the actual data.
 *  4. If only one code exists, then it is encoded using one bit.  (Zero
 *     would be more efficient, but perhaps a little confusing.)  If two
 *     codes exist, they are coded using one bit each (0 and 1).
 *  5. There is no way of sending zero distance codes--a dummy must be
 *     sent if there are none.  (History: a pre 2.0 version of PKZIP would
 *     store blocks with no distance codes, but this was discovered to be
 *     too harsh a criterion.)  Valid only for 1.93a.  2.04c does allow
 *     zero distance codes, which is sent as one code of zero bits in
 *     length.
 *  6. There are up to 286 literal/length codes.  Code 256 represents the
 *     end-of-block.  Note however that the static length tree defines
 *     288 codes just to fill out the Huffman codes.  Codes 286 and 287
 *     cannot be used though, since there is no length base or extra bits
 *     defined for them.  Similarly, there are up to 30 distance codes.
 *     However, static trees define 32 codes (all 5 bits) to fill out the
 *     Huffman codes, but the last two had better not show up in the data.
 *  7. Unzip can check dynamic Huffman blocks for complete code sets.
 *     The exception is that a single code would not be complete (see #4).
 *  8. The five bits following the block type is really the number of
 *     literal codes sent minus 257.
 *  9. Length codes 8,16,16 are interpreted as 13 length codes of 8 bits
 *     (1+6+6).  Therefore, to output three times the length, you output
 *     three codes (1+1+1), whereas to output four times the same length,
 *     you only need two codes (1+3).  Hmm.
 * 10. In the tree reconstruction algorithm, Code = Code + Increment
 *     only if BitLength(i) is not zero.  (Pretty obvious.)
 * 11. Correction: 4 Bits: # of Bit Length codes - 4     (4 - 19)
 * 12. Note: length code 284 can represent 227-258, but length code 285
 *     really is 258.  The last length deserves its own, short code
 *     since it gets used a lot in very redundant files.  The length
 *     258 is special since 258 - 3 (the min match length) is 255.
 * 13. The literal/length and distance code bit lengths are read as a
 *     single stream of lengths.  It is possible (and advantageous) for
 *     a repeat code (16, 17, or 18) to go across the boundary between
 *     the two sets of lengths.
 */

#ifdef RCSID
static char rcsid[] = "#Id: inflate.c,v 0.14 1993/06/10 13:27:04 jloup Exp #";
#endif

#ifndef __PRTOS_AARCH64__

#if defined(STDC_HEADERS) || defined(HAVE_STDLIB_H)
#  include <sys/types.h>
#  include <stdlib.h>
#endif

#include "gzip.h"

#endif /* !__PRTOS_AARCH64__ */

/*
 * Huffman code lookup table entry--this entry is four bytes for machines
 * that have 16-bit pointers (e.g. PC's in the small or medium model).
 * Valid extra bits are 0..13.  e == 15 is EOB (end of block), e == 16
 * means that v is a literal, 16 < e < 32 means that v is a pointer to
 * the next table, which codes e - 16 bits, and lastly e == 99 indicates
 * an unused code.  If a code with e == 99 is looked up, this implies an
 * error in the data.
 */
struct huft {
    uch e;                /* number of extra bits or operation */
    uch b;                /* number of bits in this code or subcode */
    union {
        ush n;              /* literal, length base, or distance base */
        struct huft *t;     /* pointer to next level of table */
    } v;
};

/* Function prototypes */
static int huft_build(unsigned *, unsigned, unsigned,
                      const ush *, const ush *, struct huft **, int *);
static int huft_free(struct huft *);
static int inflate_codes(
    struct gunzip_state *s, struct huft *tl, struct huft *td, int bl, int bd);
static int inflate_stored(struct gunzip_state *s);
static int inflate_fixed(struct gunzip_state *s);
static int inflate_dynamic(struct gunzip_state *s);
static int inflate_block(struct gunzip_state *s, int *e);
static int inflate(struct gunzip_state *s);

/*
 * The inflate algorithm uses a sliding 32 K byte window on the uncompressed
 * stream to find repeated byte strings.  This is implemented here as a
 * circular buffer.  The index is updated simply by incrementing and then
 * ANDing with 0x7fff (32K-1).
 *
 * It is left to other modules to supply the 32 K area.  It is assumed
 * to be usable as if it were declared "uch slide[32768];" or as just
 * "uch *slide;" and then malloc'ed in the latter case.  The definition
 * must be in unzip.h, included above.
 */

/* Tables for deflate from PKZIP's appnote.txt. */
static const unsigned border[] = {    /* Order of the bit length code lengths */
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
static const ush cplens[] = {         /* Copy lengths for literal codes 257..285 */
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0};
/* note: see note #13 above about the 258 in this list. */
static const ush cplext[] = {         /* Extra bits for literal codes 257..285 */
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 99, 99}; /* 99==invalid */
static const ush cpdist[] = {         /* Copy offsets for distance codes 0..29 */
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577};
static const ush cpdext[] = {         /* Extra bits for distance codes */
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
    12, 12, 13, 13};

/*
 * Macros for inflate() bit peeking and grabbing.
 * The usage is:
 *
 *      NEEDBITS(j)
 *      x = b & mask_bits[j];
 *      DUMPBITS(j)
 *
 * where NEEDBITS makes sure that b has at least j bits in it, and
 * DUMPBITS removes the bits from b.  The macros use the variable k
 * for the number of bits in b.  Normally, b and k are register
 * variables for speed, and are initialized at the beginning of a
 * routine that uses these macros from a global bit buffer and count.
 *
 * If we assume that EOB will be the longest code, then we will never
 * ask for bits with NEEDBITS that are beyond the end of the stream.
 * So, NEEDBITS should not read any more bytes than are needed to
 * meet the request.  Then no bytes need to be "returned" to the buffer
 * at the end of the last block.
 *
 * However, this assumption is not true for fixed blocks--the EOB code
 * is 7 bits, but the other literal/length codes can be 8 or 9 bits.
 * (The EOB code is shorter than other codes because fixed blocks are
 * generally short.  So, while a block always has an EOB, many other
 * literal/length codes have a significantly lower probability of
 * showing up at all.)  However, by making the first table have a
 * lookup of seven bits, the EOB code will be found in that first
 * lookup, and so will not require that too many bits be pulled from
 * the stream.
 */

static const ush mask_bits[] = {
    0x0000,
    0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
    0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};

#define NEXTBYTE(s)  ({ int v = get_byte(s); if (v < 0) goto underrun; (uch)v; })
#define NEEDBITS(s, n) {while(k<(n)){b|=((ulg)NEXTBYTE(s))<<k;k+=8;}}
#define DUMPBITS(n) {b>>=(n);k-=(n);}

/*
 * Huffman code decoding is performed using a multi-level table lookup.
 * The fastest way to decode is to simply build a lookup table whose
 * size is determined by the longest code.  However, the time it takes
 * to build this table can also be a factor if the data being decoded
 * is not very long.  The most common codes are necessarily the
 * shortest codes, so those codes dominate the decoding time, and hence
 * the speed.  The idea is you can have a shorter table that decodes the
 * shorter, more probable codes, and then point to subsidiary tables for
 * the longer codes.  The time it costs to decode the longer codes is
 * then traded against the time it takes to make longer tables.
 *
 * This results of this trade are in the variables lbits and dbits
 * below.  lbits is the number of bits the first level table for literal/
 * length codes can decode in one step, and dbits is the same thing for
 * the distance codes.  Subsequent tables are also less than or equal to
 * those sizes.  These values may be adjusted either when all of the
 * codes are shorter than that, in which case the longest code length in
 * bits is used, or when the shortest code is *longer* than the requested
 * table size, in which case the length of the shortest code in bits is
 * used.
 *
 * There are two different values for the two tables, since they code a
 * different number of possibilities each.  The literal/length table
 * codes 286 possible values, or in a flat code, a little over eight
 * bits.  The distance table codes 30 possible values, or a little less
 * than five bits, flat.  The optimum values for speed end up being
 * about one bit more than those, so lbits is 8+1 and dbits is 5+1.
 * The optimum values may differ though from machine to machine, and
 * possibly even between compilers.  Your mileage may vary.
 */

static const int lbits = 9;          /* bits in base literal/length lookup table */
static const int dbits = 6;          /* bits in base distance lookup table */

/* If BMAX needs to be larger than 16, then h and x[] should be ulg. */
#define BMAX 16         /* maximum bit length of any code (16 for explode) */
#define N_MAX 288       /* maximum number of codes in any set */

/*
 * Given a list of code lengths and a maximum table size, make a set of
 * tables to decode that set of codes.  Return zero on success, one if
 * the given code set is incomplete (the tables are still built in this
 * case), two if the input is invalid (all zero length codes or an
 * oversubscribed set of lengths), and three if not enough memory.
 *
 * @param b Code lengths in bits (all assumed <= BMAX)
 * @param n Number of codes (assumed <= N_MAX)
 * @param s Number of simple-valued codes (0..s-1)
 * @param d List of base values for non-simple codes
 * @param e List of extra bits for non-simple codes
 * @param t Result: starting table
 * @param m Maximum lookup bits, returns actual
 */
static int __init huft_build(
    unsigned *b, unsigned n, unsigned s, const ush *d, const ush *e,
    struct huft **t, int *m)
{
    unsigned a;                   /* counter for codes of length k */
    unsigned f;                   /* i repeats in table every f entries */
    int g;                        /* maximum code length */
    int h;                        /* table level */
    register unsigned i;          /* counter, current code */
    register unsigned j;          /* counter */
    register int k;               /* number of bits in current code */
    int l;                        /* bits per table (returned in m) */
    register unsigned *p;         /* pointer into c[], b[], or v[] */
    register struct huft *q;      /* points to current table */
    struct huft r;                /* table entry for structure assignment */
    register int w;               /* bits before this table == (l * h) */
    unsigned *xp;                 /* pointer into x */
    int y;                        /* number of dummy codes added */
    unsigned z;                   /* number of entries in current table */
    struct {
        unsigned c[BMAX+1];           /* bit length count table */
        struct huft *u[BMAX];         /* table stack */
        unsigned v[N_MAX];            /* values in order of bit length */
        unsigned x[BMAX+1];           /* bit offsets, then code stack */
    } *stk;
    unsigned *c, *v, *x;
    struct huft **u;
    int ret;

    DEBG("huft1 ");

    stk = malloc(sizeof(*stk));
    if (stk == NULL)
        return 3;   /* out of memory */

    c = stk->c;
    v = stk->v;
    x = stk->x;
    u = stk->u;

    /* Generate counts for each bit length */
    memzero(stk->c, sizeof(stk->c));
    p = b;  i = n;
    do {
        Tracecv(*p, (stderr, (n-i >= ' ' && n-i <= '~' ? "%c %d\n" : "0x%x %d\n"),
                     n-i, *p));
        c[*p]++;                    /* assume all entries <= BMAX */
        p++;                      /* Can't combine with above line (Solaris bug) */
    } while (--i);
    if (c[0] == n)                /* null input--all zero length codes */
    {
        *t = (struct huft *)NULL;
        *m = 0;
        ret = 2;
        goto out;
    }

    DEBG("huft2 ");

    /* Find minimum and maximum length, bound *m by those */
    l = *m;
    for (j = 1; j <= BMAX; j++)
        if (c[j])
            break;
    k = j;                        /* minimum code length */
    if ((unsigned)l < j)
        l = j;
    for (i = BMAX; i; i--)
        if (c[i])
            break;
    g = i;                        /* maximum code length */
    if ((unsigned)l > i)
        l = i;
    *m = l;

    DEBG("huft3 ");

    /* Adjust last length count to fill out codes, if needed */
    for (y = 1 << j; j < i; j++, y <<= 1)
        if ((y -= c[j]) < 0) {
            ret = 2;                 /* bad input: more codes than bits */
            goto out;
        }
    if ((y -= c[i]) < 0) {
        ret = 2;
        goto out;
    }
    c[i] += y;

    DEBG("huft4 ");

    /* Generate starting offsets into the value table for each length */
    x[1] = j = 0;
    p = c + 1;  xp = x + 2;
    while (--i) {                 /* note that i == g from above */
        *xp++ = (j += *p++);
    }

    DEBG("huft5 ");

    /* Make a table of values in order of bit lengths */
    p = b;  i = 0;
    do {
        if ((j = *p++) != 0)
            v[x[j]++] = i;
    } while (++i < n);
    n = x[g];                   /* set n to length of v */

    DEBG("h6 ");

    /* Generate the Huffman codes and for each, make the table entries */
    x[0] = i = 0;                 /* first Huffman code is zero */
    p = v;                        /* grab values in bit order */
    h = -1;                       /* no tables yet--level -1 */
    w = -l;                       /* bits decoded == (l * h) */
    u[0] = (struct huft *)NULL;   /* just to keep compilers happy */
    q = (struct huft *)NULL;      /* ditto */
    z = 0;                        /* ditto */
    DEBG("h6a ");

    /* go through the bit lengths (k already is bits in shortest code) */
    for (; k <= g; k++)
    {
        DEBG("h6b ");
        a = c[k];
        while (a--)
        {
            DEBG("h6b1 ");
            /* here i is the Huffman code of length k bits for value *p */
            /* make tables up to required level */
            while (k > w + l)
            {
                DEBG1("1 ");
                h++;
                w += l;                 /* previous table always l bits */

                /* compute minimum size table less than or equal to l bits */
                z = (z = g - w) > (unsigned)l ? l : z;  /* upper limit on table size */
                if ((f = 1 << (j = k - w)) > a + 1)     /* try a k-w bit table */
                {                       /* too few codes for k-w bit table */
                    DEBG1("2 ");
                    f -= a + 1;           /* deduct codes from patterns left */
                    xp = c + k;
                    if (j < z)
                        while (++j < z)       /* try smaller tables up to z bits */
                        {
                            if ((f <<= 1) <= *++xp)
                                break;            /* enough codes to use up j bits */
                            f -= *xp;           /* else deduct codes from patterns */
                        }
                }
                DEBG1("3 ");
                z = 1 << j;             /* table entries for j-bit table */

                /* allocate and link in new table */
                if ((q = (struct huft *)malloc((z + 1)*sizeof(struct huft))) ==
                    (struct huft *)NULL)
                {
                    if (h)
                        huft_free(u[0]);
                    ret = 3;             /* not enough memory */
                    goto out;
                }
                DEBG1("4 ");
                *t = q + 1;             /* link to list for huft_free() */
                *(t = &(q->v.t)) = (struct huft *)NULL;
                u[h] = ++q;             /* table starts after link */

                DEBG1("5 ");
                /* connect to last table, if there is one */
                if (h)
                {
                    x[h] = i;             /* save pattern for backing up */
                    r.b = (uch)l;         /* bits to dump before this table */
                    r.e = (uch)(16 + j);  /* bits in this table */
                    r.v.t = q;            /* pointer to this table */
                    j = i >> (w - l);     /* (get around Turbo C bug) */
                    u[h-1][j] = r;        /* connect to last table */
                }
                DEBG1("6 ");
            }
            DEBG("h6c ");

            /* set up table entry in r */
            r.b = (uch)(k - w);
            if (p >= v + n)
                r.e = 99;               /* out of values--invalid code */
            else if (*p < s)
            {
                r.e = (uch)(*p < 256 ? 16 : 15);    /* 256 is end-of-block code */
                r.v.n = (ush)(*p);             /* simple code is just the value */
                p++;                           /* one compiler does not like *p++ */
            }
            else
            {
                r.e = (uch)e[*p - s];   /* non-simple--look up in lists */
                r.v.n = d[*p++ - s];
            }
            DEBG("h6d ");

            /* fill code-like entries with r */
            f = 1 << (k - w);
            for (j = i >> w; j < z; j += f)
                q[j] = r;

            /* backwards increment the k-bit code i */
            for (j = 1 << (k - 1); i & j; j >>= 1)
                i ^= j;
            i ^= j;

            /* backup over finished tables */
            while ((i & ((1 << w) - 1)) != x[h])
            {
                h--;                    /* don't need to update q */
                w -= l;
            }
            DEBG("h6e ");
        }
        DEBG("h6f ");
    }

    DEBG("huft7 ");

    /* Return true (1) if we were given an incomplete table */
    ret = y != 0 && g != 1;

 out:
    free(stk);
    return ret;
}

/*
 * Free the malloc'ed tables built by huft_build(), which makes a linked
 * list of the tables it made, with the links in a dummy first entry of
 * each table.
 *
 * @param t Table to free
 */
static int __init huft_free(struct huft *t)
{
    register struct huft *p, *q;

    /* Go through linked list, freeing from the malloced (t[-1]) address. */
    p = t;
    while (p != (struct huft *)NULL)
    {
        q = (--p)->v.t;
        free((char*)p);
        p = q;
    }
    return 0;
}

/*
 * inflate (decompress) the codes in a deflated (compressed) block.
 * Return an error code or zero if it all goes ok.
 *
 * @param huft tl Literal/length decoder tables
 * @param huft td Distance decoder tables
 * @param bl  Number of bits decoded by tl[]
 * @param bd  Number of bits decoded by td[]
 */
static int __init inflate_codes(
    struct gunzip_state *s, struct huft *tl, struct huft *td, int bl, int bd)
{
    register unsigned e;  /* table entry flag/number of extra bits */
    unsigned n, d;        /* length and index for copy */
    unsigned w;           /* current window position */
    struct huft *t;       /* pointer to table entry */
    unsigned ml, md;      /* masks for bl and bd bits */
    register ulg b;       /* bit buffer */
    register unsigned k;  /* number of bits in bit buffer */


    /* make local copies of globals */
    b = s->bb;                    /* initialize bit buffer */
    k = s->bk;
    w = s->wp;                    /* initialize window position */

    /* inflate the coded data */
    ml = mask_bits[bl];           /* precompute masks for speed */
    md = mask_bits[bd];
    for (;;)                      /* do until end of block */
    {
        NEEDBITS(s, (unsigned)bl);
        if ((e = (t = tl + ((unsigned)b & ml))->e) > 16)
            do {
                if (e == 99)
                    return 1;
                DUMPBITS(t->b);
                e -= 16;
                NEEDBITS(s, e);
            } while ((e = (t = t->v.t + ((unsigned)b & mask_bits[e]))->e) > 16);
        DUMPBITS(t->b);
        if (e == 16)                /* then it's a literal */
        {
            s->window[w++] = (uch)t->v.n;
            Tracevv((stderr, "%c", s->window[w-1]));
            if (w == WSIZE)
            {
                s->wp = w;
                flush_window(s);
                w = 0;
            }
        }
        else                        /* it's an EOB or a length */
        {
            /* exit if end of block */
            if (e == 15)
                break;

            /* get length of block to copy */
            NEEDBITS(s, e);
            n = t->v.n + ((unsigned)b & mask_bits[e]);
            DUMPBITS(e);

            /* decode distance of block to copy */
            NEEDBITS(s, (unsigned)bd);
            if ((e = (t = td + ((unsigned)b & md))->e) > 16)
                do {
                    if (e == 99)
                        return 1;
                    DUMPBITS(t->b);
                    e -= 16;
                    NEEDBITS(s, e);
                } while ((e = (t = t->v.t + ((unsigned)b & mask_bits[e]))->e) > 16);
            DUMPBITS(t->b);
            NEEDBITS(s, e);
            d = w - t->v.n - ((unsigned)b & mask_bits[e]);
            DUMPBITS(e);
            Tracevv((stderr,"\\[%d,%d]", w-d, n));

            /* do the copy */
            do {
                n -= (e = (e = WSIZE - ((d &= WSIZE-1) > w ? d : w)) > n ? n : e);
                if (w - d >= e)         /* (this test assumes unsigned comparison) */
                {
                    memcpy(s->window + w, s->window + d, e);
                    w += e;
                    d += e;
                }
                else                      /* do it slow to avoid memcpy() overlap */
                    do {
                        s->window[w++] = s->window[d++];
                        Tracevv((stderr, "%c", s->window[w-1]));
                    } while (--e);
                if (w == WSIZE)
                {
                    s->wp = w;
                    flush_window(s);
                    w = 0;
                }
            } while (n);
        }
    }

    /* restore the globals from the locals */
    s->wp = w;                    /* restore global window position */
    s->bb = b;                    /* restore global bit buffer */
    s->bk = k;

    /* done */
    return 0;

 underrun:
    return 4;   /* Input underrun */
}

/* "decompress" an inflated type 0 (stored) block. */
static int __init inflate_stored(struct gunzip_state *s)
{
    unsigned n;           /* number of bytes in block */
    unsigned w;           /* current window position */
    register ulg b;       /* bit buffer */
    register unsigned k;  /* number of bits in bit buffer */

    DEBG("<stor");

    /* make local copies of globals */
    b = s->bb;                    /* initialize bit buffer */
    k = s->bk;
    w = s->wp;                    /* initialize window position */


    /* go to byte boundary */
    n = k & 7;
    DUMPBITS(n);


    /* get the length and its complement */
    NEEDBITS(s, 16);
    n = ((unsigned)b & 0xffff);
    DUMPBITS(16);
    NEEDBITS(s, 16);
    if (n != (unsigned)((~b) & 0xffff))
        return 1;                   /* error in compressed data */
    DUMPBITS(16);

    /* read and output the compressed data */
    while (n--)
    {
        NEEDBITS(s, 8);
        s->window[w++] = (uch)b;
        if (w == WSIZE)
        {
            s->wp = w;
            flush_window(s);
            w = 0;
        }
        DUMPBITS(8);
    }

    /* restore the globals from the locals */
    s->wp = w;                    /* restore global window position */
    s->bb = b;                    /* restore global bit buffer */
    s->bk = k;

    DEBG(">");
    return 0;

 underrun:
    return 4;   /* Input underrun */
}


/*
 * We use `noinline' here to prevent gcc-3.5 from using too much stack space
 */

/*
 * decompress an inflated type 1 (fixed Huffman codes) block.  We should
 * either replace this with a custom decoder, or at least precompute the
 * Huffman tables.
 */
static int noinline __init inflate_fixed(struct gunzip_state *s)
{
    int i;                /* temporary variable */
    struct huft *tl;      /* literal/length code table */
    struct huft *td;      /* distance code table */
    int bl;               /* lookup bits for tl */
    int bd;               /* lookup bits for td */
    unsigned *l;          /* length list for huft_build */

    DEBG("<fix");

    l = malloc(sizeof(*l) * 288);
    if (l == NULL)
        return 3;   /* out of memory */

    /* set up literal table */
    for (i = 0; i < 144; i++)
        l[i] = 8;
    for (; i < 256; i++)
        l[i] = 9;
    for (; i < 280; i++)
        l[i] = 7;
    for (; i < 288; i++)          /* make a complete, but wrong code set */
        l[i] = 8;
    bl = 7;
    if ((i = huft_build(l, 288, 257, cplens, cplext, &tl, &bl)) != 0) {
        free(l);
        return i;
    }

    /* set up distance table */
    for (i = 0; i < 30; i++)      /* make an incomplete code set */
        l[i] = 5;
    bd = 5;
    if ((i = huft_build(l, 30, 0, cpdist, cpdext, &td, &bd)) > 1)
    {
        huft_free(tl);
        free(l);

        DEBG(">");
        return i;
    }

    /* decompress until an end-of-block code */
    i = inflate_codes(s, tl, td, bl, bd);

    /* free the decoding tables, return */
    free(l);
    huft_free(tl);
    huft_free(td);

    return !!i;
}

/*
 * We use `noinline' here to prevent gcc-3.5 from using too much stack space
 */

/* decompress an inflated type 2 (dynamic Huffman codes) block. */
static int noinline __init inflate_dynamic(struct gunzip_state *s)
{
    int i;                /* temporary variables */
    unsigned j;
    unsigned l;           /* last length */
    unsigned m;           /* mask for bit lengths table */
    unsigned n;           /* number of lengths to get */
    struct huft *tl;      /* literal/length code table */
    struct huft *td;      /* distance code table */
    int bl;               /* lookup bits for tl */
    int bd;               /* lookup bits for td */
    unsigned nb;          /* number of bit length codes */
    unsigned nl;          /* number of literal/length codes */
    unsigned nd;          /* number of distance codes */
    unsigned *ll;         /* literal/length and distance code lengths */
    register ulg b;       /* bit buffer */
    register unsigned k;  /* number of bits in bit buffer */
    int ret;

    DEBG("<dyn");

    ll = malloc(sizeof(*ll) * (286+30));  /* literal/length and distance code lengths */

    if (ll == NULL)
        return 1;

    /* make local bit buffer */
    b = s->bb;
    k = s->bk;

    /* read in table lengths */
    NEEDBITS(s, 5);
    nl = 257 + ((unsigned)b & 0x1f);      /* number of literal/length codes */
    DUMPBITS(5);
    NEEDBITS(s, 5);
    nd = 1 + ((unsigned)b & 0x1f);        /* number of distance codes */
    DUMPBITS(5);
    NEEDBITS(s, 4);
    nb = 4 + ((unsigned)b & 0xf);         /* number of bit length codes */
    DUMPBITS(4);
    if (nl > 286 || nd > 30)
    {
        ret = 1;             /* bad lengths */
        goto out;
    }

    DEBG("dyn1 ");

    /* read in bit-length-code lengths */
    for (j = 0; j < nb; j++)
    {
        NEEDBITS(s, 3);
        ll[border[j]] = (unsigned)b & 7;
        DUMPBITS(3);
    }
    for (; j < 19; j++)
        ll[border[j]] = 0;

    DEBG("dyn2 ");

    /* build decoding table for trees--single level, 7 bit lookup */
    bl = 7;
    if ((i = huft_build(ll, 19, 19, NULL, NULL, &tl, &bl)) != 0)
    {
        if (i == 1)
            huft_free(tl);
        ret = i;                   /* incomplete code set */
        goto out;
    }

    DEBG("dyn3 ");

    /* read in literal and distance code lengths */
    n = nl + nd;
    m = mask_bits[bl];
    i = l = 0;
    while ((unsigned)i < n)
    {
        NEEDBITS(s, (unsigned)bl);
        j = (td = tl + ((unsigned)b & m))->b;
        DUMPBITS(j);
        j = td->v.n;
        if (j < 16)                 /* length of code in bits (0..15) */
            ll[i++] = l = j;          /* save last length in l */
        else if (j == 16)           /* repeat last length 3 to 6 times */
        {
            NEEDBITS(s, 2);
            j = 3 + ((unsigned)b & 3);
            DUMPBITS(2);
            if ((unsigned)i + j > n) {
                ret = 1;
                goto out;
            }
            while (j--)
                ll[i++] = l;
        }
        else if (j == 17)           /* 3 to 10 zero length codes */
        {
            NEEDBITS(s, 3);
            j = 3 + ((unsigned)b & 7);
            DUMPBITS(3);
            if ((unsigned)i + j > n) {
                ret = 1;
                goto out;
            }
            while (j--)
                ll[i++] = 0;
            l = 0;
        }
        else                        /* j == 18: 11 to 138 zero length codes */
        {
            NEEDBITS(s, 7);
            j = 11 + ((unsigned)b & 0x7f);
            DUMPBITS(7);
            if ((unsigned)i + j > n) {
                ret = 1;
                goto out;
            }
            while (j--)
                ll[i++] = 0;
            l = 0;
        }
    }

    DEBG("dyn4 ");

    /* free decoding table for trees */
    huft_free(tl);

    DEBG("dyn5 ");

    /* restore the global bit buffer */
    s->bb = b;
    s->bk = k;

    DEBG("dyn5a ");

    /* build the decoding tables for literal/length and distance codes */
    bl = lbits;
    if ((i = huft_build(ll, nl, 257, cplens, cplext, &tl, &bl)) != 0)
    {
        DEBG("dyn5b ");
        if (i == 1) {
            error("incomplete literal tree");
            huft_free(tl);
        }
        ret = i;                   /* incomplete code set */
        goto out;
    }
    DEBG("dyn5c ");
    bd = dbits;
    if ((i = huft_build(ll + nl, nd, 0, cpdist, cpdext, &td, &bd)) != 0)
    {
        DEBG("dyn5d ");
        if (i == 1) {
            error("incomplete distance tree");
            huft_free(td);
        }
        huft_free(tl);
        ret = i;                   /* incomplete code set */
        goto out;
    }

    DEBG("dyn6 ");

    /* decompress until an end-of-block code */
    ret = !!inflate_codes(s, tl, td, bl, bd);

    if ( !ret )
       DEBG("dyn7 ");

    /* free the decoding tables, return */
    huft_free(tl);
    huft_free(td);

    if ( !ret )
       DEBG(">");
 out:
    free(ll);
    return ret;

 underrun:
    ret = 4;   /* Input underrun */
    goto out;
}

/*
 * decompress an inflated block
 *
 * @param e Last block flag
 */
static int __init inflate_block(struct gunzip_state *s, int *e)
{
    unsigned t;           /* block type */
    register ulg b;       /* bit buffer */
    register unsigned k;  /* number of bits in bit buffer */

    DEBG("<blk");

    /* make local bit buffer */
    b = s->bb;
    k = s->bk;

    /* read in last block bit */
    NEEDBITS(s, 1);
    *e = (int)b & 1;
    DUMPBITS(1);

    /* read in block type */
    NEEDBITS(s, 2);
    t = (unsigned)b & 3;
    DUMPBITS(2);

    /* restore the global bit buffer */
    s->bb = b;
    s->bk = k;

    /* inflate that block type */
    if (t == 2)
        return inflate_dynamic(s);
    if (t == 0)
        return inflate_stored(s);
    if (t == 1)
        return inflate_fixed(s);

    DEBG(">");

    /* bad block type */
    return 2;

 underrun:
    return 4;   /* Input underrun */
}

/* decompress an inflated entry */
static int __init inflate(struct gunzip_state *s)
{
    int e;                /* last block flag */
    int r;                /* result code */

    /* initialize window, bit buffer */
    s->wp = 0;
    s->bk = 0;
    s->bb = 0;

    /* decompress until the last block */
    do {
        r = inflate_block(s, &e);
        if (r)
            return r;
    } while (!e);

    /* Undo too much lookahead. The next read will be byte aligned so we
     * can discard unused bits in the last meaningful byte.
     */
    while ( s->bk >= 8 )
    {
        s->bk -= 8;
        s->inptr--;
    }

    flush_window(s);

    /* return success */
    return 0;
}

/**********************************************************************
 *
 * The following are support routines for inflate.c
 *
 **********************************************************************/

/*
 * Code to compute the CRC-32 table. Borrowed from
 * gzip-1.0.3/makecrc.c.
 */

static void __init makecrc(struct gunzip_state *s)
{
/* Not copyrighted 1990 Mark Adler */

    unsigned long c;      /* crc shift register */
    unsigned long e;      /* polynomial exclusive-or pattern */
    int i;                /* counter for all possible eight bit values */
    int k;                /* byte being shifted into crc apparatus */

    /* terms of polynomial defining this crc (except x^32): */
    static const int p[] = {0,1,2,4,5,7,8,10,11,12,16,22,23,26};

    /* Make exclusive-or pattern from polynomial */
    e = 0;
    for (i = 0; i < sizeof(p)/sizeof(int); i++)
        e |= 1L << (31 - p[i]);

    s->crc_32_tab[0] = 0;

    for (i = 1; i < 256; i++)
    {
        c = 0;
        for (k = i | 256; k != 1; k >>= 1)
        {
            c = c & 1 ? (c >> 1) ^ e : c >> 1;
            if (k & 1)
                c ^= e;
        }
        s->crc_32_tab[i] = c;
    }

    s->crc = 0;
}

/* gzip flag byte */
#define ASCII_FLAG   0x01 /* bit 0 set: file probably ASCII text */
#define CONTINUATION 0x02 /* bit 1 set: continuation of multi-part gzip file */
#define EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define COMMENT      0x10 /* bit 4 set: file comment present */
#define ENCRYPTED    0x20 /* bit 5 set: file is encrypted */
#define RESERVED     0xC0 /* bit 6,7:   reserved */

/*
 * Do the uncompression!
 */
static int __init gunzip(struct gunzip_state *s)
{
    uch flags;
    unsigned char magic[2]; /* magic header */
    char method;
    ulg orig_crc = 0;       /* original crc */
    ulg orig_len = 0;       /* original uncompressed length */
    int res;

    magic[0] = NEXTBYTE(s);
    magic[1] = NEXTBYTE(s);
    method   = NEXTBYTE(s);

    if (magic[0] != 037 ||                            /* octal-ok */
        ((magic[1] != 0213) && (magic[1] != 0236))) { /* octal-ok */
        error("bad gzip magic numbers");
        return -1;
    }

    /* We only support method #8, DEFLATED */
    if (method != 8)  {
        error("internal error, invalid method");
        return -1;
    }

    flags  = (uch)get_byte(s);
    if ((flags & ENCRYPTED) != 0) {
        error("Input is encrypted");
        return -1;
    }
    if ((flags & CONTINUATION) != 0) {
        error("Multi part input");
        return -1;
    }
    if ((flags & RESERVED) != 0) {
        error("Input has invalid flags");
        return -1;
    }
    NEXTBYTE(s); /* Get timestamp */
    NEXTBYTE(s);
    NEXTBYTE(s);
    NEXTBYTE(s);

    NEXTBYTE(s); /* Ignore extra flags for the moment */
    NEXTBYTE(s); /* Ignore OS type for the moment */

    if ((flags & EXTRA_FIELD) != 0) {
        unsigned int len = NEXTBYTE(s);

        len |= (unsigned int)NEXTBYTE(s) << 8;

        while ( len-- )
            NEXTBYTE(s);
    }

    /* Get original file name if it was truncated */
    if ((flags & ORIG_NAME) != 0) {
        /* Discard the old name */
        while ( NEXTBYTE(s) != 0) /* null */
            ;
    }

    /* Discard file comment if any */
    if ((flags & COMMENT) != 0) {
        while ( NEXTBYTE(s) != 0 ) /* null */
            ;
    }

    /* Decompress */
    if ( (res = inflate(s)) )
    {
        switch (res) {
        case 0:
            break;
        case 1:
            error("invalid compressed format (err=1)");
            break;
        case 2:
            error("invalid compressed format (err=2)");
            break;
        case 3:
            error("out of memory");
            break;
        case 4:
            error("out of input data");
            break;
        default:
            error("invalid compressed format (other)");
        }
        return -1;
    }

    /* Get the crc and original length */
    /* crc32  (see algorithm.doc)
     * uncompressed input size modulo 2^32
     */
    orig_crc  = (ulg) NEXTBYTE(s);
    orig_crc |= (ulg) NEXTBYTE(s) << 8;
    orig_crc |= (ulg) NEXTBYTE(s) << 16;
    orig_crc |= (ulg) NEXTBYTE(s) << 24;

    orig_len  = (ulg) NEXTBYTE(s);
    orig_len |= (ulg) NEXTBYTE(s) << 8;
    orig_len |= (ulg) NEXTBYTE(s) << 16;
    orig_len |= (ulg) NEXTBYTE(s) << 24;

    /* Validate decompression */
    if ( orig_crc != s->crc )
    {
        error("crc error");
        return -1;
    }

    if ( orig_len != s->bytes_out )
    {
        error("length error");
        return -1;
    }
    return 0;

 underrun:   /* NEXTBYTE() goto's here if needed */
    error("out of input data");
    return -1;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: inflate.c === */

static __init void flush_window(struct gunzip_state *s)
{
    /*
     * The window is equal to the output buffer therefore only need to
     * compute the crc.
     */
    uint32_t c = ~s->crc;
    unsigned int n;
    unsigned char *in, ch;

    in = s->window;
    for ( n = 0; n < s->wp; n++ )
    {
        ch = *in++;
        c = s->crc_32_tab[(c ^ ch) & 0xff] ^ (c >> 8);
    }
    s->crc = ~c;

    s->bytes_out += s->wp;
    s->wp = 0;
}

__init int gzip_check(char *image, unsigned long image_len)
{
    unsigned char magic0, magic1;

    if ( image_len < 2 )
        return 0;

    magic0 = (unsigned char)image[0];
    magic1 = (unsigned char)image[1];

    return (magic0 == 0x1f) && ((magic1 == 0x8b) || (magic1 == 0x9e));
}

__init int perform_gunzip(char *output, char *image, unsigned long image_len)
{
    struct gunzip_state *s;
    int rc;

    if ( !gzip_check(image, image_len) )
        return 1;

    s = malloc(sizeof(struct gunzip_state));
    if ( !s )
        return -ENOMEM;

    s->window = (unsigned char *)output;
    s->inbuf = (unsigned char *)image;
    s->insize = image_len;
    s->inptr = 0;
    s->bytes_out = 0;

    makecrc(s);

    if ( gunzip(s) < 0 )
    {
        rc = -EINVAL;
    }
    else
    {
        rc = 0;
    }

    free(s);

    return rc;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: gunzip.c === */
/* === BEGIN INLINED: vsprintf.c === */
#include <prtos_prtos_config.h>
/*
 *  linux/lib/vsprintf.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/* vsprintf.c -- Lars Wirzenius & Linus Torvalds. */
/*
 * Wirzenius wrote this portably, Torvalds fucked it up :-)
 */

/* 
 * Fri Jul 13 2001 Crutcher Dunnavant <crutcher+kernel@datastacks.com>
 * - changed to provide snprintf and vsnprintf functions
 * So Feb  1 16:51:32 CET 2004 Juergen Quade <quade@hsnr.de>
 * - scnprintf and vscnprintf
 */

#include <prtos_ctype.h>
#include <prtos_symbols.h>
#include <prtos_lib.h>
#include <prtos_sched.h>
#include <prtos_livepatch.h>
#include <asm_div64.h>
#include <asm_page.h>

static int skip_atoi(const char **s)
{
    int i=0;

    while (isdigit(**s))
        i = i*10 + *((*s)++) - '0';
    return i;
}

#define ZEROPAD 1               /* pad with zero */
#define SIGN    2               /* unsigned/signed long */
#define PLUS    4               /* show plus */
#define SPACE   8               /* space if plus */
#define LEFT    16              /* left justified */
#define SPECIAL 32              /* 0x */
#define LARGE   64              /* use 'ABCDEF' instead of 'abcdef' */

static char *number(
    char *buf, const char *end, unsigned long long num,
    int base, int size, int precision, int type)
{
    char c,sign,tmp[66];
    const char *digits;
    static const char small_digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    static const char large_digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int i;

    ASSERT(base >= 2 && base <= 36);

    digits = (type & LARGE) ? large_digits : small_digits;
    if (type & LEFT)
        type &= ~ZEROPAD;
    c = (type & ZEROPAD) ? '0' : ' ';
    sign = 0;
    if (type & SIGN) {
        if ((signed long long) num < 0) {
            sign = '-';
            num = - (signed long long) num;
            size--;
        } else if (type & PLUS) {
            sign = '+';
            size--;
        } else if (type & SPACE) {
            sign = ' ';
            size--;
        }
    }
    if (type & SPECIAL) {
        if (num == 0)
            type &= ~SPECIAL;
        else if (base == 16)
            size -= 2;
        else if (base == 8)
            size--;
        else
            type &= ~SPECIAL;
    }
    i = 0;
    if (num == 0)
        tmp[i++]='0';
    else while (num != 0)
        tmp[i++] = digits[do_div(num,base)];
    if (i > precision)
        precision = i;
    size -= precision;
    if (!(type&(ZEROPAD+LEFT))) {
        while(size-->0) {
            if (buf < end)
                *buf = ' ';
            ++buf;
        }
    }
    if (sign) {
        if (buf < end)
            *buf = sign;
        ++buf;
    }
    if (type & SPECIAL) {
        if (buf < end)
            *buf = '0';
        ++buf;
        if (base == 16) {
            if (buf < end)
                *buf = digits[33];
            ++buf;
        }
    }
    if (!(type & LEFT)) {
        while (size-- > 0) {
            if (buf < end)
                *buf = c;
            ++buf;
        }
    }
    while (i < precision--) {
        if (buf < end)
            *buf = '0';
        ++buf;
    }
    while (i-- > 0) {
        if (buf < end)
            *buf = tmp[i];
        ++buf;
    }
    while (size-- > 0) {
        if (buf < end)
            *buf = ' ';
        ++buf;
    }
    return buf;
}

static char *string(char *str, const char *end, const char *s,
                    int field_width, int precision, int flags)
{
    int i, len = (precision < 0) ? strlen(s) : strnlen(s, precision);

    if (!(flags & LEFT)) {
        while (len < field_width--) {
            if (str < end)
                *str = ' ';
            ++str;
        }
    }
    for (i = 0; i < len; ++i) {
        if (str < end)
            *str = *s;
        ++str; ++s;
    }
    while (len < field_width--) {
        if (str < end)
            *str = ' ';
        ++str;
    }

    return str;
}

/* Print a bitmap as '0-3,6-15' */
static char *print_bitmap_list(char *str, const char *end,
                               const unsigned long *bitmap,
                               unsigned int nr_bits)
{
    /* current bit is 'cur', most recently seen range is [rbot, rtop] */
    unsigned int cur, rbot, rtop;
    bool first = true;

    rbot = cur = find_first_bit(bitmap, nr_bits);
    while ( cur < nr_bits )
    {
        rtop = cur;
        cur = find_next_bit(bitmap, nr_bits, cur + 1);

        if ( cur < nr_bits && cur <= rtop + 1 )
            continue;

        if ( !first )
        {
            if ( str < end )
                *str = ',';
            str++;
        }
        first = false;

        str = number(str, end, rbot, 10, -1, -1, 0);
        if ( rbot < rtop )
        {
            if ( str < end )
                *str = '-';
            str++;

            str = number(str, end, rtop, 10, -1, -1, 0);
        }

        rbot = cur;
    }

    return str;
}

/* Print a bitmap as a comma separated hex string. */
static char *print_bitmap_string(char *str, const char *end,
                                 const unsigned long *bitmap,
                                 unsigned int nr_bits)
{
    const unsigned int CHUNKSZ = 32;
    unsigned int chunksz;
    int i;
    bool first = true;

    chunksz = nr_bits & (CHUNKSZ - 1);
    if ( chunksz == 0 )
        chunksz = CHUNKSZ;

    /*
     * First iteration copes with the trailing partial word if nr_bits isn't a
     * round multiple of CHUNKSZ.  All subsequent iterations work on a
     * complete CHUNKSZ block.
     */
    for ( i = ROUNDUP(nr_bits, CHUNKSZ) - CHUNKSZ; i >= 0; i -= CHUNKSZ )
    {
        unsigned int chunkmask = (1ULL << chunksz) - 1;
        unsigned int word      = i / BITS_PER_LONG;
        unsigned int offset    = i % BITS_PER_LONG;
        unsigned long val      = (bitmap[word] >> offset) & chunkmask;

        if ( !first )
        {
            if ( str < end )
                *str = ',';
            str++;
        }
        first = false;

        str = number(str, end, val, 16, DIV_ROUND_UP(chunksz, 4), -1, ZEROPAD);

        chunksz = CHUNKSZ;
    }

    return str;
}

/* Print a domain id, using names for system domains.  (e.g. d0 or d[IDLE]) */
static char *print_domain(char *str, const char *end, const struct domain *d)
{
    const char *name = NULL;

    /* Some debugging may have an optionally-NULL pointer. */
    if ( unlikely(!d) )
        return string(str, end, "NULL", -1, -1, 0);

    switch ( d->domain_id )
    {
    case DOMID_IO:   name = "[IO]";   break;
    case DOMID_PRTOS:  name = "[PRTOS]";  break;
    case DOMID_COW:  name = "[COW]";  break;
    case DOMID_IDLE: name = "[IDLE]"; break;
        /*
         * In principle, we could ASSERT_UNREACHABLE() in the default case.
         * However, this path is used to print out crash information, which
         * risks recursing infinitely and not printing any useful information.
         */
    }

    if ( str < end )
        *str = 'd';

    if ( name )
        return string(str + 1, end, name, -1, -1, 0);
    else
        return number(str + 1, end, d->domain_id, 10, -1, -1, 0);
}

/* Print a vcpu id.  (e.g. d0v1 or d[IDLE]v0) */
static char *print_vcpu(char *str, const char *end, const struct vcpu *v)
{
    /* Some debugging may have an optionally-NULL pointer. */
    if ( unlikely(!v) )
        return string(str, end, "NULL", -1, -1, 0);

    str = print_domain(str, end, v->domain);

    if ( str < end )
        *str = 'v';

    return number(str + 1, end, v->vcpu_id, 10, -1, -1, 0);
}

static char *print_pci_addr(char *str, const char *end, const pci_sbdf_t *sbdf)
{
    str = number(str, end, sbdf->seg, 16, 4, -1, ZEROPAD);
    if ( str < end )
        *str = ':';
    str = number(str + 1, end, sbdf->bus, 16, 2, -1, ZEROPAD);
    if ( str < end )
        *str = ':';
    str = number(str + 1, end, sbdf->dev, 16, 2, -1, ZEROPAD);
    if ( str < end )
        *str = '.';
    return number(str + 1, end, sbdf->fn, 8, -1, -1, 0);
}

static char *pointer(char *str, const char *end, const char **fmt_ptr,
                     const void *arg, int field_width, int precision,
                     int flags)
{
    const char *fmt = *fmt_ptr, *s;

    /* Custom %p suffixes. See PRTOS_ROOT/docs/misc/printk-formats.txt */
    switch ( fmt[1] )
    {
    case 'b': /* Bitmap as hex, or list */
        ++*fmt_ptr;

        if ( field_width < 0 )
            return str;

        if ( fmt[2] == 'l' )
        {
            ++*fmt_ptr;

            return print_bitmap_list(str, end, arg, field_width);
        }

        return print_bitmap_string(str, end, arg, field_width);

    case 'd': /* Domain ID from a struct domain *. */
        ++*fmt_ptr;
        return print_domain(str, end, arg);

    case 'h': /* Raw buffer as hex string. */
    {
        const uint8_t *hex_buffer = arg;
        char sep = ' '; /* Separator character. */
        unsigned int i;

        /* Consumed 'h' from the format string. */
        ++*fmt_ptr;

        /* Bound user count from %* to between 0 and 64 bytes. */
        if ( field_width <= 0 )
            return str;
        if ( field_width > 64 )
            field_width = 64;

        /*
         * Peek ahead in the format string to see if a recognised separator
         * modifier is present.
         */
        switch ( fmt[2] )
        {
        case 'C': /* Colons. */
            ++*fmt_ptr;
            sep = ':';
            break;

        case 'D': /* Dashes. */
            ++*fmt_ptr;
            sep = '-';
            break;

        case 'N': /* No separator. */
            ++*fmt_ptr;
            sep = 0;
            break;
        }

        for ( i = 0; ; )
        {
            /* Each byte: 2 chars, 0-padded, base 16, no hex prefix. */
            str = number(str, end, hex_buffer[i], 16, 2, -1, ZEROPAD);

            if ( ++i == field_width )
                break;

            if ( sep )
            {
                if ( str < end )
                    *str = sep;
                ++str;
            }
        }

        return str;
    }

    case 'p': /* PCI SBDF. */
        ++*fmt_ptr;
        return print_pci_addr(str, end, arg);

    case 's': /* Symbol name with offset and size (iff offset != 0) */
    case 'S': /* Symbol name unconditionally with offset and size */
    {
        unsigned long sym_size, sym_offset;
        char namebuf[KSYM_NAME_LEN+1];

        /* Advance parents fmt string, as we have consumed 's' or 'S' */
        ++*fmt_ptr;

        s = symbols_lookup((unsigned long)arg, &sym_size, &sym_offset, namebuf);

        /* If the symbol is not found, fall back to printing the address */
        if ( !s )
            break;

        /* Print symbol name */
        str = string(str, end, s, -1, -1, 0);

        if ( fmt[1] == 'S' || sym_offset != 0 )
        {
            /* Print '+<offset>/<len>' */
            str = number(str, end, sym_offset, 16, -1, -1, SPECIAL|SIGN|PLUS);
            if ( str < end )
                *str = '/';
            ++str;
            str = number(str, end, sym_size, 16, -1, -1, SPECIAL);
        }

        /*
         * namebuf contents and s for core hypervisor are same but for Live Patch
         * payloads they differ (namebuf contains the name of the payload).
         */
        if ( namebuf != s )
        {
            str = string(str, end, " [", -1, -1, 0);
            str = string(str, end, namebuf, -1, -1, 0);
            str = string(str, end, "]", -1, -1, 0);
        }

        return str;
    }

    case 'v': /* d<domain-id>v<vcpu-id> from a struct vcpu */
        ++*fmt_ptr;
        return print_vcpu(str, end, arg);
    }

    if ( field_width == -1 )
    {
        field_width = 2 * sizeof(void *);
        flags |= ZEROPAD;
    }

    return number(str, end, (unsigned long)arg,
                  16, field_width, precision, flags);
}

/**
 * vsnprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @args: Arguments for the format string
 *
 * The return value is the number of characters which would
 * be generated for the given input, excluding the trailing
 * '\0', as per ISO C99. If you want to have the exact
 * number of characters written into @buf as return value
 * (not including the trailing '\0'), use vscnprintf. If the
 * return is greater than or equal to @size, the resulting
 * string is truncated.
 *
 * Call this function if you are already dealing with a va_list.
 * You probably want snprintf instead.
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    unsigned long long num;
    int base;
    char *str, *end, c;
    const char *s;

    int flags;          /* flags to number() */

    int field_width;    /* width of output field */
    int precision;              /* min. # of digits for integers; max
                                   number of chars for from string */
    int qualifier;              /* 'h', 'l', or 'L' for integer fields */
                                /* 'z' support added 23/7/1999 S.H.    */
                                /* 'z' changed to 'Z' --davidm 1/25/99 */

    /* Reject out-of-range values early */
    BUG_ON(((int)size < 0) || ((unsigned int)size != size));

    str = buf;
    end = buf + size;

    if (end < buf) {
        end = ((void *) -1);
        size = end - buf;
    }

    for (; *fmt ; ++fmt) {
        if (*fmt != '%') {
            if (str < end)
                *str = *fmt;
            ++str;
            continue;
        }

        /* process flags */
        flags = 0;
    repeat:
        ++fmt;          /* this also skips first '%' */
        switch (*fmt) {
        case '-': flags |= LEFT; goto repeat;
        case '+': flags |= PLUS; goto repeat;
        case ' ': flags |= SPACE; goto repeat;
        case '#': flags |= SPECIAL; goto repeat;
        case '0': flags |= ZEROPAD; goto repeat;
        }

        /* get field width */
        field_width = -1;
        if (isdigit(*fmt))
            field_width = skip_atoi(&fmt);
        else if (*fmt == '*') {
            ++fmt;
            /* it's the next argument */
            field_width = va_arg(args, int);
            if (field_width < 0) {
                field_width = -field_width;
                flags |= LEFT;
            }
        }

        /* get the precision */
        precision = -1;
        if (*fmt == '.') {
            ++fmt;
            if (isdigit(*fmt))
                precision = skip_atoi(&fmt);
            else if (*fmt == '*') {
                ++fmt;
                          /* it's the next argument */
                precision = va_arg(args, int);
            }
            if (precision < 0)
                precision = 0;
        }

        /* get the conversion qualifier */
        qualifier = -1;
        if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L' ||
            *fmt =='Z' || *fmt == 'z') {
            qualifier = *fmt;
            ++fmt;
            if (qualifier == 'l' && *fmt == 'l') {
                qualifier = 'L';
                ++fmt;
            }
        }

        /* default base */
        base = 10;

        switch (*fmt) {
        case 'c':
            if (!(flags & LEFT)) {
                while (--field_width > 0) {
                    if (str < end)
                        *str = ' ';
                    ++str;
                }
            }
            c = (unsigned char) va_arg(args, int);
            if (str < end)
                *str = c;
            ++str;
            while (--field_width > 0) {
                if (str < end)
                    *str = ' ';
                ++str;
            }
            continue;

        case 's':
            s = va_arg(args, char *);
            if ((unsigned long)s < PAGE_SIZE)
                s = "<NULL>";

            str = string(str, end, s, field_width, precision, flags);
            continue;

        case 'p':
            /* pointer() might advance fmt (%pS for example) */
            str = pointer(str, end, &fmt, va_arg(args, const void *),
                          field_width, precision, flags);
            continue;


        case 'n':
            if (qualifier == 'l') {
                long * ip = va_arg(args, long *);
                *ip = (str - buf);
            } else if (qualifier == 'Z' || qualifier == 'z') {
                size_t * ip = va_arg(args, size_t *);
                *ip = (str - buf);
            } else {
                int * ip = va_arg(args, int *);
                *ip = (str - buf);
            }
            continue;

        case '%':
            if (str < end)
                *str = '%';
            ++str;
            continue;

            /* integer number formats - set up the flags and "break" */
        case 'o':
            base = 8;
            break;

        case 'X':
            flags |= LARGE;
            fallthrough;
        case 'x':
            base = 16;
            break;

        case 'd':
        case 'i':
            flags |= SIGN;
            fallthrough;
        case 'u':
            break;

        default:
            if (str < end)
                *str = '%';
            ++str;
            if (*fmt) {
                if (str < end)
                    *str = *fmt;
                ++str;
            } else {
                --fmt;
            }
            continue;
        }
        if (qualifier == 'L')
            num = va_arg(args, long long);
        else if (qualifier == 'l') {
            num = va_arg(args, unsigned long);
            if (flags & SIGN)
                num = (signed long) num;
        } else if (qualifier == 'Z' || qualifier == 'z') {
            num = va_arg(args, size_t);
        } else if (qualifier == 'h') {
            num = (unsigned short) va_arg(args, int);
            if (flags & SIGN)
                num = (signed short) num;
        } else {
            num = va_arg(args, unsigned int);
            if (flags & SIGN)
                num = (signed int) num;
        }

        str = number(str, end, num, base,
                     field_width, precision, flags);
    }

    /* don't write out a null byte if the buf size is zero */
    if (size > 0) {
        if (str < end)
            *str = '\0';
        else
            end[-1] = '\0';
    }
    /* the trailing null byte doesn't count towards the total
     * ++str;
     */
    return str-buf;
}

EXPORT_SYMBOL(vsnprintf);

/**
 * vscnprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @args: Arguments for the format string
 *
 * The return value is the number of characters which have been written into
 * the @buf not including the trailing '\0'. If @size is <= 0 the function
 * returns 0.
 *
 * Call this function if you are already dealing with a va_list.
 * You probably want scnprintf instead.
 */
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    int i;

    i = vsnprintf(buf,size,fmt,args);
    if (i >= size)
        i = size - 1;
    return (i > 0) ? i : 0;
}

EXPORT_SYMBOL(vscnprintf);

/**
 * snprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @...: Arguments for the format string
 *
 * The return value is the number of characters which would be
 * generated for the given input, excluding the trailing null,
 * as per ISO C99.  If the return is greater than or equal to
 * @size, the resulting string is truncated.
 */
// int snprintf(char * buf, size_t size, const char *fmt, ...)
// {
//     va_list args;
//     int i;

//     va_start(args, fmt);
//     i=vsnprintf(buf,size,fmt,args);
//     va_end(args);
//     return i;
// }

// EXPORT_SYMBOL(snprintf);

/**
 * scnprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @...: Arguments for the format string
 *
 * The return value is the number of characters written into @buf not including
 * the trailing '\0'. If @size is <= 0 the function returns 0. If the return is
 * greater than or equal to @size, the resulting string is truncated.
 */

int scnprintf(char * buf, size_t size, const char *fmt, ...)
{
    va_list args;
    int i;

    va_start(args, fmt);
    i = vsnprintf(buf, size, fmt, args);
    va_end(args);
    if (i >= size)
        i = size - 1;
    return (i > 0) ? i : 0;
}
EXPORT_SYMBOL(scnprintf);



/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

/* === END INLINED: vsprintf.c === */
/* === BEGIN INLINED: notifier.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * common/notifier.c
 *
 * Routines to manage notifier chains for passing status changes to any
 * interested routines.
 *
 * Original code from Linux kernel 2.6.27 (Alan Cox <Alan.Cox@linux.org>)
 */

#include <prtos_init.h>
#include <prtos_notifier.h>

/**
 * notifier_chain_register - Add notifier to a raw notifier chain
 * @nh: Pointer to head of the raw notifier chain
 * @n: New entry in notifier chain
 *
 * Adds a notifier to a raw notifier chain.
 * All locking must be provided by the caller.
 */
void __init notifier_chain_register(
    struct notifier_head *nh, struct notifier_block *n)
{
    struct list_head *chain = &nh->head;
    struct notifier_block *nb;

    while ( chain->next != &nh->head )
    {
        nb = list_entry(chain->next, struct notifier_block, chain);
        if ( n->priority > nb->priority )
            break;
        chain = chain->next;
    }

    list_add(&n->chain, chain);
}


/**
 * notifier_call_chain - Informs the registered notifiers about an event.
 * @nh: Pointer to head of the raw notifier chain
 * @val:  Value passed unmodified to notifier function
 * @v:  Pointer passed unmodified to notifier function
 * @pcursor: If non-NULL, position in chain to start from. Also updated on
 *           return to indicate how far notifications got before stopping.
 *
 * Calls each function in a notifier chain in turn.  The functions run in an
 * undefined context. All locking must be provided by the caller.
 *
 * If the return value of the notifier can be and'ed with %NOTIFY_STOP_MASK
 * then notifier_call_chain() will return immediately, with teh return value of
 * the notifier function which halted execution. Otherwise the return value is
 * the return value of the last notifier function called.
 */
int notifier_call_chain(
    struct notifier_head *nh, unsigned long val, void *v,
    struct notifier_block **pcursor)
{
    int ret = NOTIFY_DONE;
    struct list_head *cursor;
    struct notifier_block *nb = NULL;
    bool reverse = val & NOTIFY_REVERSE;

    cursor = pcursor && *pcursor ? &(*pcursor)->chain : &nh->head;

    do {
        cursor = reverse ? cursor->prev : cursor->next;
        if ( cursor == &nh->head )
            break;
        nb = list_entry(cursor, struct notifier_block, chain);
        printk("00 notifier_call_chain: %s: %p\n", nb->notifier_call ? "called" : "NULL", nb );
        ret = nb->notifier_call(nb, val, v);
        //printk("11 notifier_call_chain: %s: %p ret=0x%x\n",   nb->notifier_call ? "called" : "NULL", nb, ret);
    } while ( !(ret & NOTIFY_STOP_MASK) );

    if ( pcursor )
        *pcursor = nb;

    return ret;
}

/* === END INLINED: notifier.c === */
/* === BEGIN INLINED: preempt.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * preempt.c
 * 
 * Track atomic regions in the hypervisor which disallow sleeping.
 * 
 * Copyright (c) 2010, Keir Fraser <keir@xen.org>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <prtos_preempt.h>
#include <prtos_irq.h>
#include <asm_system.h>

DEFINE_PER_CPU(unsigned int, __preempt_count);


#ifndef NDEBUG
void ASSERT_NOT_IN_ATOMIC(void)
{
    ASSERT(!preempt_count());
    ASSERT(!in_irq());
    ASSERT(local_irq_is_enabled());
}
#endif

/* === END INLINED: preempt.c === */
/* === BEGIN INLINED: rcupdate.c === */
#include <prtos_prtos_config.h>
/*
 * Read-Copy Update mechanism for mutual exclusion
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) IBM Corporation, 2001
 *
 * Authors: Dipankar Sarma <dipankar@in.ibm.com>
 *          Manfred Spraul <manfred@colorfullife.com>
 * 
 * Modifications for PRTOS: Jose Renato Santos
 * Copyright (C) Hewlett-Packard, 2006
 *
 * Based on the original work by Paul McKenney <paulmck@us.ibm.com>
 * and inputs from Rusty Russell, Andrea Arcangeli and Andi Kleen.
 * Papers:
 * http://www.rdrop.com/users/paulmck/paper/rclockpdcsproof.pdf
 * http://lse.sourceforge.net/locking/rclock_OLS.2001.05.01c.sc.pdf (OLS2001)
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * http://lse.sourceforge.net/locking/rcupdate.html
 */
#include <prtos_types.h>
#include <prtos_kernel.h>
#include <prtos_init.h>
#include <prtos_param.h>
#include <prtos_spinlock.h>
#include <prtos_smp.h>
#include <prtos_rcupdate.h>
#include <prtos_sched.h>
#include <asm_atomic.h>
#include <prtos_bitops.h>
#include <prtos_percpu.h>
#include <prtos_softirq.h>
#include <prtos_cpu.h>
#include <prtos_stop_machine.h>

DEFINE_PER_CPU(unsigned int, rcu_lock_cnt);

/* Global control variables for rcupdate callback mechanism. */
static struct rcu_ctrlblk {
    long cur;           /* Current batch number.                      */
    long completed;     /* Number of the last completed batch         */
    int  next_pending;  /* Is the next batch already waiting?         */

    spinlock_t  lock __cacheline_aligned;
    cpumask_t   cpumask; /* CPUs that need to switch in order ... */
    cpumask_t   idle_cpumask; /* ... unless they are already idle */
    /* for current batch to proceed.        */
} __cacheline_aligned rcu_ctrlblk = {
    .cur = -300,
    .completed = -300,
    .lock = SPIN_LOCK_UNLOCKED,
};

/*
 * Per-CPU data for Read-Copy Update.
 * nxtlist - new callbacks are added here
 * curlist - current batch for which quiescent cycle started if any
 */
struct rcu_data {
    /* 1) quiescent state handling : */
    long quiescbatch;    /* Batch # for grace period */
    int  qs_pending;     /* core waits for quiesc state */

    /* 2) batch handling */
    long            batch;            /* Batch # for current RCU batch */
    struct rcu_head *nxtlist;
    struct rcu_head **nxttail;
    long            qlen;             /* # of queued callbacks */
    struct rcu_head *curlist;
    struct rcu_head **curtail;
    struct rcu_head *donelist;
    struct rcu_head **donetail;
    long            blimit;           /* Upper limit on a processed batch */
    int cpu;
    long            last_rs_qlen;     /* qlen during the last resched */

    /* 3) idle CPUs handling */
    struct timer idle_timer;
    bool idle_timer_active;

    bool            process_callbacks;
    bool            barrier_active;
};

/*
 * If a CPU with RCU callbacks queued goes idle, when the grace period is
 * not finished yet, how can we make sure that the callbacks will eventually
 * be executed? In Linux (2.6.21, the first "tickless idle" Linux kernel),
 * the periodic timer tick would not be stopped for such CPU. Here in PRTOS,
 * we (may) don't even have a periodic timer tick, so we need to use a
 * special purpose timer.
 *
 * Such timer:
 * 1) is armed only when a CPU with an RCU callback(s) queued goes idle
 *    before the end of the current grace period (_not_ for any CPUs that
 *    go idle!);
 * 2) when it fires, it is only re-armed if the grace period is still
 *    running;
 * 3) it is stopped immediately, if the CPU wakes up from idle and
 *    resumes 'normal' execution.
 *
 * About how far in the future the timer should be programmed each time,
 * it's hard to tell (guess!!). Since this mimics Linux's periodic timer
 * tick, take values used there as an indication. In Linux 2.6.21, tick
 * period can be 10ms, 4ms, 3.33ms or 1ms.
 *
 * By default, we use 10ms, to enable at least some power saving on the
 * CPU that is going idle. The user can change this, via a boot time
 * parameter, but only up to 100ms.
 */
#define IDLE_TIMER_PERIOD_MAX     MILLISECS(100)
#define IDLE_TIMER_PERIOD_DEFAULT MILLISECS(10)
#define IDLE_TIMER_PERIOD_MIN     MICROSECS(100)

static s_time_t __read_mostly idle_timer_period;

/*
 * Increment and decrement values for the idle timer handler. The algorithm
 * works as follows:
 * - if the timer actually fires, and it finds out that the grace period isn't
 *   over yet, we add IDLE_TIMER_PERIOD_INCR to the timer's period;
 * - if the timer actually fires and it finds the grace period over, we
 *   subtract IDLE_TIMER_PERIOD_DECR from the timer's period.
 */
#define IDLE_TIMER_PERIOD_INCR    MILLISECS(10)
#define IDLE_TIMER_PERIOD_DECR    MICROSECS(100)

static DEFINE_PER_CPU(struct rcu_data, rcu_data);

static int blimit = 10;
static int qhimark = 10000;
static int qlowmark = 100;
static int rsinterval = 1000;

/*
 * rcu_barrier() handling:
 * Two counters are used to synchronize rcu_barrier() work:
 * - cpu_count holds the number of cpus required to finish barrier handling.
 *   It is decremented by each cpu when it has performed all pending rcu calls.
 * - pending_count shows whether any rcu_barrier() activity is running and
 *   it is used to synchronize leaving rcu_barrier() only after all cpus
 *   have finished their processing. pending_count is initialized to nr_cpus + 1
 *   and it is decremented by each cpu when it has seen that cpu_count has
 *   reached 0. The cpu where rcu_barrier() has been called will wait until
 *   pending_count has been decremented to 1 (so all cpus have seen cpu_count
 *   reaching 0) and will then set pending_count to 0 indicating there is no
 *   rcu_barrier() running.
 * Cpus are synchronized via softirq mechanism. rcu_barrier() is regarded to
 * be active if pending_count is not zero. In case rcu_barrier() is called on
 * multiple cpus it is enough to check for pending_count being not zero on entry
 * and to call process_pending_softirqs() in a loop until pending_count drops to
 * zero, before starting the new rcu_barrier() processing.
 */
static atomic_t cpu_count = ATOMIC_INIT(0);
static atomic_t pending_count = ATOMIC_INIT(0);

static void cf_check rcu_barrier_callback(struct rcu_head *head)
{
    /*
     * We need a barrier making all previous writes visible to other cpus
     * before doing the atomic_dec(). This would be something like
     * smp_mb__before_atomic() limited to writes, which isn't existing.
     * So we choose the best alternative available which is smp_wmb()
     * (correct on Arm and only a minor impact on x86, while
     * smp_mb__before_atomic() would be correct on x86, but with a larger
     * impact on Arm).
     */
    smp_wmb();
    atomic_dec(&cpu_count);
}

static void rcu_barrier_action(void)
{
    struct rcu_head head;

    /*
     * When callback is executed, all previously-queued RCU work on this CPU
     * is completed. When all CPUs have executed their callback, cpu_count
     * will have been decremented to 0.
     */
    call_rcu(&head, rcu_barrier_callback);

    while ( atomic_read(&cpu_count) )
    {
        process_pending_softirqs();
        cpu_relax();
    }

    smp_mb__before_atomic();
    atomic_dec(&pending_count);
}

void rcu_barrier(void)
{
    unsigned int n_cpus;

    ASSERT(!in_irq() && local_irq_is_enabled());

    for ( ; ; )
    {
        if ( !atomic_read(&pending_count) && get_cpu_maps() )
        {
            n_cpus = num_online_cpus();

            if ( atomic_cmpxchg(&pending_count, 0, n_cpus + 1) == 0 )
                break;

            put_cpu_maps();
        }

        process_pending_softirqs();
        cpu_relax();
    }

    atomic_set(&cpu_count, n_cpus);
    cpumask_raise_softirq(&cpu_online_map, RCU_SOFTIRQ);

    while ( atomic_read(&pending_count) != 1 )
    {
        process_pending_softirqs();
        cpu_relax();
    }

    atomic_set(&pending_count, 0);

    put_cpu_maps();
}

/* Is batch a before batch b ? */
static inline int rcu_batch_before(long a, long b)
{
    return (a - b) < 0;
}

static void force_quiescent_state(struct rcu_data *rdp,
                                  struct rcu_ctrlblk *rcp)
{
    cpumask_t cpumask;
    raise_softirq(RCU_SOFTIRQ);
    if (unlikely(rdp->qlen - rdp->last_rs_qlen > rsinterval)) {
        rdp->last_rs_qlen = rdp->qlen;
        /*
         * Don't send IPI to itself. With irqs disabled,
         * rdp->cpu is the current cpu.
         */
        cpumask_andnot(&cpumask, &rcp->cpumask, cpumask_of(rdp->cpu));
        cpumask_raise_softirq(&cpumask, RCU_SOFTIRQ);
    }
}

/**
 * call_rcu - Queue an RCU callback for invocation after a grace period.
 * @head: structure to be used for queueing the RCU updates.
 * @func: actual update function to be invoked after the grace period
 *
 * The update function will be invoked some time after a full grace
 * period elapses, in other words after all currently executing RCU
 * read-side critical sections have completed.  RCU read-side critical
 * sections are delimited by rcu_read_lock() and rcu_read_unlock(),
 * and may be nested.
 */
void call_rcu(struct rcu_head *head,
              void (*func)(struct rcu_head *rcu))
{
    unsigned long flags;
    struct rcu_data *rdp;

    head->func = func;
    head->next = NULL;
    local_irq_save(flags);
    rdp = &this_cpu(rcu_data);
    *rdp->nxttail = head;
    rdp->nxttail = &head->next;
    if (unlikely(++rdp->qlen > qhimark)) {
        rdp->blimit = INT_MAX;
        force_quiescent_state(rdp, &rcu_ctrlblk);
    }
    local_irq_restore(flags);
}

/*
 * Invoke the completed RCU callbacks. They are expected to be in
 * a per-cpu list.
 */
static void rcu_do_batch(struct rcu_data *rdp)
{
    struct rcu_head *next, *list;
    int count = 0;

    list = rdp->donelist;
    while (list) {
        next = rdp->donelist = list->next;
        list->func(list);
        list = next;
        rdp->qlen--;
        if (++count >= rdp->blimit)
            break;
    }
    if (rdp->blimit == INT_MAX && rdp->qlen <= qlowmark)
        rdp->blimit = blimit;
    if (!rdp->donelist)
        rdp->donetail = &rdp->donelist;
    else
    {
        rdp->process_callbacks = true;
        raise_softirq(RCU_SOFTIRQ);
    }
}

/*
 * Grace period handling:
 * The grace period handling consists out of two steps:
 * - A new grace period is started.
 *   This is done by rcu_start_batch. The start is not broadcasted to
 *   all cpus, they must pick this up by comparing rcp->cur with
 *   rdp->quiescbatch. All cpus are recorded  in the
 *   rcu_ctrlblk.cpumask bitmap.
 * - All cpus must go through a quiescent state.
 *   Since the start of the grace period is not broadcasted, at least two
 *   calls to rcu_check_quiescent_state are required:
 *   The first call just notices that a new grace period is running. The
 *   following calls check if there was a quiescent state since the beginning
 *   of the grace period. If so, it updates rcu_ctrlblk.cpumask. If
 *   the bitmap is empty, then the grace period is completed.
 *   rcu_check_quiescent_state calls rcu_start_batch(0) to start the next grace
 *   period (if necessary).
 */
/*
 * Register a new batch of callbacks, and start it up if there is currently no
 * active batch and the batch to be registered has not already occurred.
 * Caller must hold rcu_ctrlblk.lock.
 */
static void rcu_start_batch(struct rcu_ctrlblk *rcp)
{
    if (rcp->next_pending &&
        rcp->completed == rcp->cur) {
        rcp->next_pending = 0;
        /*
         * next_pending == 0 must be visible in
         * __rcu_process_callbacks() before it can see new value of cur.
         */
        smp_wmb();
        rcp->cur++;

       /*
        * Make sure the increment of rcp->cur is visible so, even if a
        * CPU that is about to go idle, is captured inside rcp->cpumask,
        * rcu_pending() will return false, which then means cpu_quiet()
        * will be invoked, before the CPU would actually enter idle.
        *
        * This barrier is paired with the one in rcu_idle_enter().
        */
        smp_mb();
        cpumask_andnot(&rcp->cpumask, &cpu_online_map, &rcp->idle_cpumask);
    }
}

/*
 * cpu went through a quiescent state since the beginning of the grace period.
 * Clear it from the cpu mask and complete the grace period if it was the last
 * cpu. Start another grace period if someone has further entries pending
 */
static void cpu_quiet(int cpu, struct rcu_ctrlblk *rcp)
{
    cpumask_clear_cpu(cpu, &rcp->cpumask);
    if (cpumask_empty(&rcp->cpumask)) {
        /* batch completed ! */
        rcp->completed = rcp->cur;
        rcu_start_batch(rcp);
    }
}

/*
 * Check if the cpu has gone through a quiescent state (say context
 * switch). If so and if it already hasn't done so in this RCU
 * quiescent cycle, then indicate that it has done so.
 */
static void rcu_check_quiescent_state(struct rcu_ctrlblk *rcp,
                                      struct rcu_data *rdp)
{
    if (rdp->quiescbatch != rcp->cur) {
        /* start new grace period: */
        rdp->qs_pending = 1;
        rdp->quiescbatch = rcp->cur;
        return;
    }

    /* Grace period already completed for this cpu?
     * qs_pending is checked instead of the actual bitmap to avoid
     * cacheline trashing.
     */
    if (!rdp->qs_pending)
        return;

    rdp->qs_pending = 0;

    spin_lock(&rcp->lock);
    /*
     * rdp->quiescbatch/rcp->cur and the cpu bitmap can come out of sync
     * during cpu startup. Ignore the quiescent state.
     */
    if (likely(rdp->quiescbatch == rcp->cur))
        cpu_quiet(rdp->cpu, rcp);

    spin_unlock(&rcp->lock);
}


/*
 * This does the RCU processing work from softirq context. 
 */
static void __rcu_process_callbacks(struct rcu_ctrlblk *rcp,
                                    struct rcu_data *rdp)
{
    if (rdp->curlist && !rcu_batch_before(rcp->completed, rdp->batch)) {
        *rdp->donetail = rdp->curlist;
        rdp->donetail = rdp->curtail;
        rdp->curlist = NULL;
        rdp->curtail = &rdp->curlist;
    }

    local_irq_disable();
    if (rdp->nxtlist && !rdp->curlist) {
        rdp->curlist = rdp->nxtlist;
        rdp->curtail = rdp->nxttail;
        rdp->nxtlist = NULL;
        rdp->nxttail = &rdp->nxtlist;
        local_irq_enable();

        /*
         * start the next batch of callbacks
         */

        /* determine batch number */
        rdp->batch = rcp->cur + 1;
        /* see the comment and corresponding wmb() in
         * the rcu_start_batch()
         */
        smp_rmb();

        if (!rcp->next_pending) {
            /* and start it/schedule start if it's a new batch */
            spin_lock(&rcp->lock);
            rcp->next_pending = 1;
            rcu_start_batch(rcp);
            spin_unlock(&rcp->lock);
        }
    } else {
        local_irq_enable();
    }
    rcu_check_quiescent_state(rcp, rdp);
    if (rdp->donelist)
        rcu_do_batch(rdp);
}

static void cf_check rcu_process_callbacks(void)
{
    struct rcu_data *rdp = &this_cpu(rcu_data);

    if ( rdp->process_callbacks )
    {
        rdp->process_callbacks = false;
        __rcu_process_callbacks(&rcu_ctrlblk, rdp);
    }

    if ( atomic_read(&cpu_count) && !rdp->barrier_active )
    {
        rdp->barrier_active = true;
        rcu_barrier_action();
        rdp->barrier_active = false;
    }
}

static int __rcu_pending(struct rcu_ctrlblk *rcp, struct rcu_data *rdp)
{
    /* This cpu has pending rcu entries and the grace period
     * for them has completed.
     */
    if (rdp->curlist && !rcu_batch_before(rcp->completed, rdp->batch))
        return 1;

    /* This cpu has no pending entries, but there are new entries */
    if (!rdp->curlist && rdp->nxtlist)
        return 1;

    /* This cpu has finished callbacks to invoke */
    if (rdp->donelist)
        return 1;

    /* The rcu core waits for a quiescent state from the cpu */
    if (rdp->quiescbatch != rcp->cur || rdp->qs_pending)
        return 1;

    /* nothing to do */
    return 0;
}

int rcu_pending(int cpu)
{
    return __rcu_pending(&rcu_ctrlblk, &per_cpu(rcu_data, cpu));
}

/*
 * Check to see if any future RCU-related work will need to be done
 * by the current CPU, even if none need be done immediately, returning
 * 1 if so.  This function is part of the RCU implementation; it is -not-
 * an exported member of the RCU API.
 */
int rcu_needs_cpu(int cpu)
{
    struct rcu_data *rdp = &per_cpu(rcu_data, cpu);

    return (rdp->curlist && !rdp->idle_timer_active) || rcu_pending(cpu);
}

/*
 * Timer for making sure the CPU where a callback is queued does
 * periodically poke rcu_pedning(), so that it will invoke the callback
 * not too late after the end of the grace period.
 */
static void rcu_idle_timer_start(void)
{
    struct rcu_data *rdp = &this_cpu(rcu_data);

    /*
     * Note that we don't check rcu_pending() here. In fact, we don't want
     * the timer armed on CPUs that are in the process of quiescing while
     * going idle, unless they really are the ones with a queued callback.
     */
    if (likely(!rdp->curlist))
        return;

    set_timer(&rdp->idle_timer, NOW() + idle_timer_period);
    rdp->idle_timer_active = true;
}

static void rcu_idle_timer_stop(void)
{
    struct rcu_data *rdp = &this_cpu(rcu_data);

    if (likely(!rdp->idle_timer_active))
        return;

    rdp->idle_timer_active = false;

    /*
     * In general, as the CPU is becoming active again, we don't need the
     * idle timer, and so we want to stop it.
     *
     * However, in case we are here because idle_timer has (just) fired and
     * has woken up the CPU, we skip stop_timer() now. In fact, when a CPU
     * wakes up from idle, this code always runs before do_softirq() has the
     * chance to check and deal with TIMER_SOFTIRQ. And if we stop the timer
     * now, the TIMER_SOFTIRQ handler will see it as inactive, and will not
     * call rcu_idle_timer_handler().
     *
     * Therefore, if we see that the timer is expired already, we leave it
     * alone. The TIMER_SOFTIRQ handler will then run the timer routine, and
     * deactivate it.
     */
    if ( !timer_is_expired(&rdp->idle_timer) )
        stop_timer(&rdp->idle_timer);
}

static void cf_check rcu_idle_timer_handler(void* data)
{
    perfc_incr(rcu_idle_timer);

    if ( !cpumask_empty(&rcu_ctrlblk.cpumask) )
        idle_timer_period = min(idle_timer_period + IDLE_TIMER_PERIOD_INCR,
                                IDLE_TIMER_PERIOD_MAX);
    else
        idle_timer_period = max(idle_timer_period - IDLE_TIMER_PERIOD_DECR,
                                IDLE_TIMER_PERIOD_MIN);
}

void rcu_check_callbacks(int cpu)
{
    struct rcu_data *rdp = &this_cpu(rcu_data);

    rdp->process_callbacks = true;
    raise_softirq(RCU_SOFTIRQ);
}

static void rcu_move_batch(struct rcu_data *this_rdp, struct rcu_head *list,
                           struct rcu_head **tail)
{
    local_irq_disable();
    *this_rdp->nxttail = list;
    if (list)
        this_rdp->nxttail = tail;
    local_irq_enable();
}

static void rcu_offline_cpu(struct rcu_data *this_rdp,
                            struct rcu_ctrlblk *rcp, struct rcu_data *rdp)
{
    kill_timer(&rdp->idle_timer);

    /* If the cpu going offline owns the grace period we can block
     * indefinitely waiting for it, so flush it here.
     */
    spin_lock(&rcp->lock);
    if (rcp->cur != rcp->completed)
        cpu_quiet(rdp->cpu, rcp);
    spin_unlock(&rcp->lock);

    rcu_move_batch(this_rdp, rdp->donelist, rdp->donetail);
    rcu_move_batch(this_rdp, rdp->curlist, rdp->curtail);
    rcu_move_batch(this_rdp, rdp->nxtlist, rdp->nxttail);

    local_irq_disable();
    this_rdp->qlen += rdp->qlen;
    local_irq_enable();
}

static void rcu_init_percpu_data(int cpu, struct rcu_ctrlblk *rcp,
                                 struct rcu_data *rdp)
{
    memset(rdp, 0, sizeof(*rdp));
    rdp->curtail = &rdp->curlist;
    rdp->nxttail = &rdp->nxtlist;
    rdp->donetail = &rdp->donelist;
    rdp->quiescbatch = rcp->completed;
    rdp->qs_pending = 0;
    rdp->cpu = cpu;
    rdp->blimit = blimit;
    init_timer(&rdp->idle_timer, rcu_idle_timer_handler, rdp, cpu);
}

static int cf_check cpu_callback(
    struct notifier_block *nfb, unsigned long action, void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;
    struct rcu_data *rdp = &per_cpu(rcu_data, cpu);

    switch ( action )
    {
    case CPU_UP_PREPARE:
        rcu_init_percpu_data(cpu, &rcu_ctrlblk, rdp);
        break;
    case CPU_UP_CANCELED:
    case CPU_DEAD:
        rcu_offline_cpu(&this_cpu(rcu_data), &rcu_ctrlblk, rdp);
        break;
    default:
        break;
    }

    return NOTIFY_DONE;
}

void rcu_init_percpu_data_prtos(int cpu) {
    struct rcu_data *rdp = &per_cpu(rcu_data, cpu);
    rcu_init_percpu_data(cpu, &rcu_ctrlblk, rdp);
}

static struct notifier_block cpu_nfb = {
    .notifier_call = cpu_callback
};

void __init rcu_init(void)
{
    void *cpu = (void *)(long)smp_processor_id();
    static unsigned int __initdata idle_timer_period_ms =
                                    IDLE_TIMER_PERIOD_DEFAULT / MILLISECS(1);
    integer_param("rcu-idle-timer-period-ms", idle_timer_period_ms);

    /* We don't allow 0, or anything higher than IDLE_TIMER_PERIOD_MAX */
    if ( idle_timer_period_ms == 0 ||
         idle_timer_period_ms > IDLE_TIMER_PERIOD_MAX / MILLISECS(1) )
    {
        idle_timer_period_ms = IDLE_TIMER_PERIOD_DEFAULT / MILLISECS(1);
        printk("WARNING: rcu-idle-timer-period-ms outside of "
               "(0,%"PRI_stime"]. Resetting it to %u.\n",
               IDLE_TIMER_PERIOD_MAX / MILLISECS(1), idle_timer_period_ms);
    }
    idle_timer_period = MILLISECS(idle_timer_period_ms);

    cpumask_clear(&rcu_ctrlblk.idle_cpumask);
    cpu_callback(&cpu_nfb, CPU_UP_PREPARE, cpu);
    register_cpu_notifier(&cpu_nfb);
    open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
}

/*
 * The CPU is becoming idle, so no more read side critical
 * sections, and one more step toward grace period.
 */
void rcu_idle_enter(unsigned int cpu)
{
    ASSERT(!cpumask_test_cpu(cpu, &rcu_ctrlblk.idle_cpumask));
    cpumask_set_cpu(cpu, &rcu_ctrlblk.idle_cpumask);
    /*
     * If some other CPU is starting a new grace period, we'll notice that
     * by seeing a new value in rcp->cur (different than our quiescbatch).
     * That will force us all the way until cpu_quiet(), clearing our bit
     * in rcp->cpumask, even in case we managed to get in there.
     *
     * Se the comment before cpumask_andnot() in  rcu_start_batch().
     */
    smp_mb();

    rcu_idle_timer_start();
}

void rcu_idle_exit(unsigned int cpu)
{
    rcu_idle_timer_stop();
    ASSERT(cpumask_test_cpu(cpu, &rcu_ctrlblk.idle_cpumask));
    cpumask_clear_cpu(cpu, &rcu_ctrlblk.idle_cpumask);
}

/* === END INLINED: rcupdate.c === */
/* === BEGIN INLINED: rwlock.c === */
#include <prtos_prtos_config.h>
#include <prtos_rwlock.h>
#include <prtos_irq.h>

/*
 * rspin_until_writer_unlock - spin until writer is gone.
 * @lock  : Pointer to queue rwlock structure.
 * @cnts: Current queue rwlock writer status byte.
 *
 * In interrupt context or at the head of the queue, the reader will just
 * increment the reader count & wait until the writer releases the lock.
 */
static inline void rspin_until_writer_unlock(rwlock_t *lock, u32 cnts)
{
    while ( (cnts & _QW_WMASK) == _QW_LOCKED )
    {
        cpu_relax();
        smp_rmb();
        cnts = atomic_read(&lock->cnts);
    }
}

/*
 * queue_read_lock_slowpath - acquire read lock of a queue rwlock.
 * @lock: Pointer to queue rwlock structure.
 */
void queue_read_lock_slowpath(rwlock_t *lock)
{
    u32 cnts;

    /*
     * Readers come here when they cannot get the lock without waiting.
     */
    atomic_sub(_QR_BIAS, &lock->cnts);

    /*
     * Put the reader into the wait queue.
     *
     * Use the speculation unsafe helper, as it's the caller responsibility to
     * issue a speculation barrier if required.
     */
    _spin_lock(&lock->lock);

    /*
     * At the head of the wait queue now, wait until the writer state
     * goes to 0 and then try to increment the reader count and get
     * the lock. It is possible that an incoming writer may steal the
     * lock in the interim, so it is necessary to check the writer byte
     * to make sure that the write lock isn't taken.
     */
    while ( atomic_read(&lock->cnts) & _QW_WMASK )
        cpu_relax();

    cnts = atomic_add_return(_QR_BIAS, &lock->cnts);
    rspin_until_writer_unlock(lock, cnts);

    /*
     * Signal the next one in queue to become queue head.
     */
    spin_unlock(&lock->lock);

    lock_enter(&lock->lock.debug);
}

/*
 * queue_write_lock_slowpath - acquire write lock of a queue rwlock
 * @lock : Pointer to queue rwlock structure.
 */
void queue_write_lock_slowpath(rwlock_t *lock)
{
    u32 cnts;

    /*
     * Put the writer into the wait queue.
     *
     * Use the speculation unsafe helper, as it's the caller responsibility to
     * issue a speculation barrier if required.
     */
    _spin_lock(&lock->lock);

    /* Try to acquire the lock directly if no reader is present. */
    if ( !atomic_read(&lock->cnts) &&
         (atomic_cmpxchg(&lock->cnts, 0, _write_lock_val()) == 0) )
        goto unlock;

    /*
     * Set the waiting flag to notify readers that a writer is pending,
     * or wait for a previous writer to go away.
     */
    for ( ; ; )
    {
        cnts = atomic_read(&lock->cnts);
        if ( !(cnts & _QW_WMASK) &&
             (atomic_cmpxchg(&lock->cnts, cnts,
                             cnts | _QW_WAITING) == cnts) )
            break;

        cpu_relax();
    }

    /* When no more readers, set the locked flag. */
    for ( ; ; )
    {
        cnts = atomic_read(&lock->cnts);
        if ( (cnts == _QW_WAITING) &&
             (atomic_cmpxchg(&lock->cnts, _QW_WAITING,
                             _write_lock_val()) == _QW_WAITING) )
            break;

        cpu_relax();
    }
 unlock:
    spin_unlock(&lock->lock);

    lock_enter(&lock->lock.debug);
}


static DEFINE_PER_CPU(cpumask_t, percpu_rwlock_readers);

void _percpu_write_lock(percpu_rwlock_t **per_cpudata,
                percpu_rwlock_t *percpu_rwlock)
{
    unsigned int cpu;
    cpumask_t *rwlock_readers = &this_cpu(percpu_rwlock_readers);

    /* Validate the correct per_cpudata variable has been provided. */
    _percpu_rwlock_owner_check(per_cpudata, percpu_rwlock);

    /*
     * First take the write lock to protect against other writers or slow
     * path readers.
     *
     * Note we use the speculation unsafe variant of write_lock(), as the
     * calling wrapper already adds a speculation barrier after the lock has
     * been taken.
     */
    _write_lock(&percpu_rwlock->rwlock);

    /* Now set the global variable so that readers start using read_lock. */
    percpu_rwlock->writer_activating = 1;
    smp_mb();

    /* Using a per cpu cpumask is only safe if there is no nesting. */
    ASSERT(!in_irq());
    cpumask_copy(rwlock_readers, &cpu_online_map);

    /* Check if there are any percpu readers in progress on this rwlock. */
    for ( ; ; )
    {
        for_each_cpu(cpu, rwlock_readers)
        {
            /*
             * Remove any percpu readers not contending on this rwlock
             * from our check mask.
             */
            if ( per_cpu_ptr(per_cpudata, cpu) != percpu_rwlock )
                __cpumask_clear_cpu(cpu, rwlock_readers);
        }
        /* Check if we've cleared all percpu readers from check mask. */
        if ( cpumask_empty(rwlock_readers) )
            break;
        /* Give the coherency fabric a break. */
        cpu_relax();
    };

    lock_enter(&percpu_rwlock->rwlock.lock.debug);
}

/* === END INLINED: rwlock.c === */
/* === BEGIN INLINED: spinlock.c === */
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>

#include <prtos_cpu.h>
#include <prtos_lib.h>
#include <prtos_irq.h>
#include <prtos_notifier.h>
#include <prtos_param.h>
#include <prtos_smp.h>
#include <prtos_time.h>
#include <prtos_spinlock.h>
#include <prtos_guest_access.h>
#include <prtos_preempt.h>
#include <public_sysctl.h>
#include <asm_processor.h>
#include <asm_atomic.h>

#ifdef CONFIG_DEBUG_LOCKS

/* Max. number of entries in locks_taken array. */
static unsigned int __ro_after_init lock_depth_size = 64;
integer_param("lock-depth-size", lock_depth_size);

/*
 * Array of addresses of taken locks.
 * nr_locks_taken is the index after the last entry. As locks tend to be
 * nested cleanly, when freeing a lock it will probably be the one before
 * nr_locks_taken, and new entries can be entered at that index. It is fine
 * for a lock to be released out of order, though.
 */
static DEFINE_PER_CPU(const union lock_debug **, locks_taken);
static DEFINE_PER_CPU(unsigned int, nr_locks_taken);
static bool __read_mostly max_depth_reached;

static atomic_t spin_debug __read_mostly = ATOMIC_INIT(0);
static union lock_debug * array[64 * 4];
static int lock_index = 0;
static int cf_check cpu_lockdebug_callback(struct notifier_block *nfb,
                                           unsigned long action,
                                           void *hcpu)
{
    unsigned int cpu = (unsigned long)hcpu;

    switch ( action )
    {
    case CPU_UP_PREPARE:
        printk("cpu_lockdebug_callback: lock_depth_size: %u\n", lock_depth_size);
        // if ( !per_cpu(locks_taken, cpu) )
        //     per_cpu(locks_taken, cpu) = xzalloc_array(const union lock_debug *,
        //                                               lock_depth_size);

        if ( !per_cpu(locks_taken, cpu) ) {
            per_cpu(locks_taken, cpu) = &array[64 * (lock_index++)];
            printk("per_cpu(locks_taken, cpu): 0x%x\n", (unsigned int)per_cpu(locks_taken, cpu));
        }
        if(lock_index >= 64 * 4)
            printk("lock_index overflow: %d\n", lock_index);

        if ( !per_cpu(locks_taken, cpu) )
            printk(PRTOSLOG_WARNING
                   "cpu %u: failed to allocate lock recursion check area\n",
                   cpu);
        break;

    case CPU_UP_CANCELED:
    case CPU_DEAD:
        XFREE(per_cpu(locks_taken, cpu));
        break;

    default:
        break;
    }

    return 0;
}

void cpu_lockdebug_init_prtos(int cpu) {
    printk("cpu_lockdebug_callback: lock_depth_size: %u\n", lock_depth_size);
    // if ( !per_cpu(locks_taken, cpu) )
    //     per_cpu(locks_taken, cpu) = xzalloc_array(const union lock_debug *,
    //                                               lock_depth_size);

    if (!per_cpu(locks_taken, cpu)) {
        per_cpu(locks_taken, cpu) = &array[64 * (lock_index++)];
        printk("per_cpu(locks_taken, cpu): 0x%x\n", (unsigned int)per_cpu(locks_taken, cpu));
    }
    if (lock_index >= 64 * 4) printk("lock_index overflow: %d\n", lock_index);

    if (!per_cpu(locks_taken, cpu)) printk(PRTOSLOG_WARNING "cpu %u: failed to allocate lock recursion check area\n", cpu);
}

static struct notifier_block cpu_lockdebug_nfb = {
    .notifier_call = cpu_lockdebug_callback,
};

static int __init cf_check lockdebug_init(void)
{
    if ( lock_depth_size )
    {
        register_cpu_notifier(&cpu_lockdebug_nfb);
        cpu_lockdebug_callback(&cpu_lockdebug_nfb, CPU_UP_PREPARE,
                               (void *)(unsigned long)smp_processor_id());
    }

    return 0;
}
presmp_initcall(lockdebug_init);

void check_lock(union lock_debug *debug, bool try)
{
    bool irq_safe = !local_irq_is_enabled();
    unsigned int cpu = smp_processor_id();
    const union lock_debug *const *taken = per_cpu(locks_taken, cpu);
    unsigned int nr_taken = per_cpu(nr_locks_taken, cpu);
    unsigned int i;

    BUILD_BUG_ON(LOCK_DEBUG_PAD_BITS <= 0);

    if ( unlikely(atomic_read(&spin_debug) <= 0) )
        return;

    /* A few places take liberties with this. */
    /* BUG_ON(in_irq() && !irq_safe); */

    /*
     * We partition locks into IRQ-safe (always held with IRQs disabled) and
     * IRQ-unsafe (always held with IRQs enabled) types. The convention for
     * every lock must be consistently observed else we can deadlock in
     * IRQ-context rendezvous functions (a rendezvous which gets every CPU
     * into IRQ context before any CPU is released from the rendezvous).
     *
     * If we can mix IRQ-disabled and IRQ-enabled callers, the following can
     * happen:
     *  * Lock is held by CPU A, with IRQs enabled
     *  * CPU B is spinning on same lock, with IRQs disabled
     *  * Rendezvous starts -- CPU A takes interrupt and enters rendezbous spin
     *  * DEADLOCK -- CPU B will never enter rendezvous, CPU A will never exit
     *                the rendezvous, and will hence never release the lock.
     *
     * To guard against this subtle bug we latch the IRQ safety of every
     * spinlock in the system, on first use.
     *
     * A spin_trylock() with interrupts off is always fine, as this can't
     * block and above deadlock scenario doesn't apply.
     */
    if ( try && irq_safe )
        return;

    if ( unlikely(debug->irq_safe != irq_safe) )
    {
        union lock_debug seen, new = { 0 };

        new.irq_safe = irq_safe;
        seen.val = cmpxchg(&debug->val, LOCK_DEBUG_INITVAL, new.val);

        if ( !seen.unseen && seen.irq_safe == !irq_safe )
        {
            printk("CHECKLOCK FAILURE: prev irqsafe: %d, curr irqsafe %d\n",
                   seen.irq_safe, irq_safe);
            BUG();
        }
    }

    if ( try )
        return;

    for ( i = 0; i < nr_taken; i++ )
        if ( taken[i] == debug )
        {
            printk("CHECKLOCK FAILURE: lock at %p taken recursively\n", debug);
            BUG();
        }
}

static void check_barrier(union lock_debug *debug)
{
    if ( unlikely(atomic_read(&spin_debug) <= 0) )
        return;

    /*
     * For a barrier, we have a relaxed IRQ-safety-consistency check.
     *
     * It is always safe to spin at the barrier with IRQs enabled -- that does
     * not prevent us from entering an IRQ-context rendezvous, and nor are
     * we preventing anyone else from doing so (since we do not actually
     * acquire the lock during a barrier operation).
     *
     * However, if we spin on an IRQ-unsafe lock with IRQs disabled then that
     * is clearly wrong, for the same reason outlined in check_lock() above.
     */
    BUG_ON(!local_irq_is_enabled() && !debug->irq_safe);
}

void lock_enter(const union lock_debug *debug)
{
    unsigned int cpu = smp_processor_id();
    const union lock_debug **taken = per_cpu(locks_taken, cpu);
    unsigned int *nr_taken = &per_cpu(nr_locks_taken, cpu);
    unsigned long flags;

    if ( !taken )
        return;

    local_irq_save(flags);

    if ( *nr_taken < lock_depth_size )
        taken[(*nr_taken)++] = debug;
    else if ( !max_depth_reached )
    {
        max_depth_reached = true;
        printk("CHECKLOCK max lock depth %u reached!\n", lock_depth_size);
        WARN();
    }

    local_irq_restore(flags);
}

void lock_exit(const union lock_debug *debug)
{
    unsigned int cpu = smp_processor_id();
    const union lock_debug **taken = per_cpu(locks_taken, cpu);
    unsigned int *nr_taken = &per_cpu(nr_locks_taken, cpu);
    unsigned int i;
    unsigned long flags;

    if ( !taken )
        return;

    local_irq_save(flags);

    for ( i = *nr_taken; i > 0; i-- )
    {
        if ( taken[i - 1] == debug )
        {
            memmove(taken + i - 1, taken + i,
                    (*nr_taken - i) * sizeof(*taken));
            (*nr_taken)--;
            taken[*nr_taken] = NULL;

            local_irq_restore(flags);

            return;
        }
    }

    if ( !max_depth_reached )
    {
        printk("CHECKLOCK released lock at %p not recorded!\n", debug);
        WARN();
    }

    local_irq_restore(flags);
}

static void got_lock(union lock_debug *debug)
{
    debug->cpu = smp_processor_id();

    lock_enter(debug);
}

static void rel_lock(union lock_debug *debug)
{
    if ( atomic_read(&spin_debug) > 0 )
        BUG_ON(debug->cpu != smp_processor_id());

    lock_exit(debug);

    debug->cpu = SPINLOCK_NO_CPU;
}

void spin_debug_enable(void)
{
    atomic_inc(&spin_debug);
}

void spin_debug_disable(void)
{
    atomic_dec(&spin_debug);
}

#else /* CONFIG_DEBUG_LOCKS */

#define check_barrier(l) ((void)0)
#define got_lock(l) ((void)0)
#define rel_lock(l) ((void)0)

#endif

#ifdef CONFIG_DEBUG_LOCK_PROFILE

#define LOCK_PROFILE_PAR lock->profile
#define LOCK_PROFILE_REL                                                     \
    if ( profile )                                                           \
    {                                                                        \
        profile->time_hold += NOW() - profile->time_locked;                  \
        profile->lock_cnt++;                                                 \
    }
#define LOCK_PROFILE_VAR(var, val)    s_time_t var = (val)
#define LOCK_PROFILE_BLOCK(var)       (var) = (var) ? : NOW()
#define LOCK_PROFILE_BLKACC(tst, val)                                        \
    if ( tst )                                                               \
    {                                                                        \
        profile->time_block += profile->time_locked - (val);                 \
        profile->block_cnt++;                                                \
    }
#define LOCK_PROFILE_GOT(val)                                                \
    if ( profile )                                                           \
    {                                                                        \
        profile->time_locked = NOW();                                        \
        LOCK_PROFILE_BLKACC(val, val);                                       \
    }

#else

#define LOCK_PROFILE_PAR NULL
#define LOCK_PROFILE_REL
#define LOCK_PROFILE_VAR(var, val)
#define LOCK_PROFILE_BLOCK(var)
#define LOCK_PROFILE_BLKACC(tst, val)
#define LOCK_PROFILE_GOT(val)

#endif

static always_inline spinlock_tickets_t observe_lock(spinlock_tickets_t *t)
{
    spinlock_tickets_t v;

    smp_rmb();
    v.head_tail = read_atomic(&t->head_tail);
    return v;
}

static always_inline uint16_t observe_head(const spinlock_tickets_t *t)
{
    smp_rmb();
    return read_atomic(&t->head);
}

/**
 * WA to fix the link error when compiling with
 * aarch64-linux-gnu-ld: /usr/lib/gcc-cross/aarch64-linux-gnu/13/libgcc.a(lse-init.o): in function `init_have_lse_atomics':
(.text.startup+0x10): undefined reference to `__getauxval'
(.text.startup+0x10): relocation truncated to fit: R_AARCH64_CALL26 against undefined symbol `__getauxval'
 */
unsigned long __getauxval(unsigned long type)
{
    return 0;
}

static void always_inline spin_lock_common(spinlock_tickets_t *t,
                                           union lock_debug *debug,
                                           struct lock_profile *profile,
                                           void (*cb)(void *data), void *data)
{
    spinlock_tickets_t tickets = SPINLOCK_TICKET_INC;
    LOCK_PROFILE_VAR(block, 0);

    check_lock(debug, false);
    preempt_disable();
    tickets.head_tail = arch_fetch_and_add(&t->head_tail, tickets.head_tail);
    while ( tickets.tail != observe_head(t) )
    {
        LOCK_PROFILE_BLOCK(block);
        if ( cb )
            cb(data);
        arch_lock_relax();
    }
    arch_lock_acquire_barrier();
    got_lock(debug);
    LOCK_PROFILE_GOT(block);
}

void _spin_lock(spinlock_t *lock)
{
    spin_lock_common(&lock->tickets, &lock->debug, LOCK_PROFILE_PAR, NULL,
                     NULL);
}


void _spin_lock_irq(spinlock_t *lock)
{
    ASSERT(local_irq_is_enabled());
    local_irq_disable();
    _spin_lock(lock);
}

unsigned long _spin_lock_irqsave(spinlock_t *lock)
{
    unsigned long flags;

    local_irq_save(flags);
    _spin_lock(lock);
    return flags;
}

static void always_inline spin_unlock_common(spinlock_tickets_t *t,
                                             union lock_debug *debug,
                                             struct lock_profile *profile)
{
    LOCK_PROFILE_REL;
    rel_lock(debug);
    arch_lock_release_barrier();
    add_sized(&t->head, 1);
    arch_lock_signal();
    preempt_enable();
}

void _spin_unlock(spinlock_t *lock)
{
    spin_unlock_common(&lock->tickets, &lock->debug, LOCK_PROFILE_PAR);
}

void _spin_unlock_irq(spinlock_t *lock)
{
    _spin_unlock(lock);
    local_irq_enable();
}

void _spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
    _spin_unlock(lock);
    local_irq_restore(flags);
}

static bool always_inline spin_is_locked_common(const spinlock_tickets_t *t)
{
    return t->head != t->tail;
}

bool _spin_is_locked(const spinlock_t *lock)
{
    /*
     * This function is suitable only for use in ASSERT()s and alike, as it
     * doesn't tell _who_ is holding the lock.
     */
    return spin_is_locked_common(&lock->tickets);
}

static bool always_inline spin_trylock_common(spinlock_tickets_t *t,
                                              union lock_debug *debug,
                                              struct lock_profile *profile)
{
    spinlock_tickets_t old, new;

    preempt_disable();
    check_lock(debug, true);
    old = observe_lock(t);
    if ( old.head != old.tail )
    {
        preempt_enable();
        return false;
    }
    new = old;
    new.tail++;
    if ( cmpxchg(&t->head_tail, old.head_tail, new.head_tail) != old.head_tail )
    {
        preempt_enable();
        return false;
    }
    /*
     * cmpxchg() is a full barrier so no need for an
     * arch_lock_acquire_barrier().
     */
    got_lock(debug);
    LOCK_PROFILE_GOT(0);

    return true;
}

bool _spin_trylock(spinlock_t *lock)
{
    return spin_trylock_common(&lock->tickets, &lock->debug, LOCK_PROFILE_PAR);
}

static void always_inline spin_barrier_common(spinlock_tickets_t *t,
                                              union lock_debug *debug,
                                              struct lock_profile *profile)
{
    spinlock_tickets_t sample;
    LOCK_PROFILE_VAR(block, NOW());

    check_barrier(debug);
    smp_mb();
    sample = observe_lock(t);
    if ( sample.head != sample.tail )
    {
        while ( observe_head(t) == sample.head )
            arch_lock_relax();
        LOCK_PROFILE_BLKACC(profile, block);
    }
    smp_mb();
}


bool _rspin_is_locked(const rspinlock_t *lock)
{
    /*
     * Recursive locks may be locked by another CPU, yet we return
     * "false" here, making this function suitable only for use in
     * ASSERT()s and alike.
     */
    return lock->recurse_cpu == SPINLOCK_NO_CPU
           ? spin_is_locked_common(&lock->tickets)
           : lock->recurse_cpu == smp_processor_id();
}

void _rspin_barrier(rspinlock_t *lock)
{
    spin_barrier_common(&lock->tickets, &lock->debug, LOCK_PROFILE_PAR);
}


void _rspin_lock(rspinlock_t *lock)
{
    unsigned int cpu = smp_processor_id();

    if ( likely(lock->recurse_cpu != cpu) )
    {
        spin_lock_common(&lock->tickets, &lock->debug, LOCK_PROFILE_PAR, NULL,
                         NULL);
        lock->recurse_cpu = cpu;
    }

    /* We support only fairly shallow recursion, else the counter overflows. */
    ASSERT(lock->recurse_cnt < SPINLOCK_MAX_RECURSE);
    lock->recurse_cnt++;
}

unsigned long _rspin_lock_irqsave(rspinlock_t *lock)
{
    unsigned long flags;

    local_irq_save(flags);
    _rspin_lock(lock);

    return flags;
}

void _rspin_unlock(rspinlock_t *lock)
{
    if ( likely(--lock->recurse_cnt == 0) )
    {
        lock->recurse_cpu = SPINLOCK_NO_CPU;
        spin_unlock_common(&lock->tickets, &lock->debug, LOCK_PROFILE_PAR);
    }
}

void _rspin_unlock_irqrestore(rspinlock_t *lock, unsigned long flags)
{
    _rspin_unlock(lock);
    local_irq_restore(flags);
}


void _nrspin_lock(rspinlock_t *lock)
{
    spin_lock_common(&lock->tickets, &lock->debug, LOCK_PROFILE_PAR, NULL,
                     NULL);
}

void _nrspin_unlock(rspinlock_t *lock)
{
    spin_unlock_common(&lock->tickets, &lock->debug, LOCK_PROFILE_PAR);
}

void _nrspin_lock_irq(rspinlock_t *lock)
{
    ASSERT(local_irq_is_enabled());
    local_irq_disable();
    _nrspin_lock(lock);
}

void _nrspin_unlock_irq(rspinlock_t *lock)
{
    _nrspin_unlock(lock);
    local_irq_enable();
}

unsigned long _nrspin_lock_irqsave(rspinlock_t *lock)
{
    unsigned long flags;

    local_irq_save(flags);
    _nrspin_lock(lock);

    return flags;
}

void _nrspin_unlock_irqrestore(rspinlock_t *lock, unsigned long flags)
{
    _nrspin_unlock(lock);
    local_irq_restore(flags);
}

#ifdef CONFIG_DEBUG_LOCK_PROFILE

struct lock_profile_anc {
    struct lock_profile_qhead *head_q;   /* first head of this type */
    const char                *name;     /* descriptive string for print */
};

typedef void lock_profile_subfunc(struct lock_profile *data, int32_t type,
    int32_t idx, void *par);

extern struct lock_profile *__lock_profile_start;
extern struct lock_profile *__lock_profile_end;

static s_time_t lock_profile_start;
static struct lock_profile_anc lock_profile_ancs[] = {
    [LOCKPROF_TYPE_GLOBAL] = { .name = "Global" },
    [LOCKPROF_TYPE_PERDOM] = { .name = "Domain" },
};
static struct lock_profile_qhead lock_profile_glb_q;
static spinlock_t lock_profile_lock = SPIN_LOCK_UNLOCKED;

static void spinlock_profile_iterate(lock_profile_subfunc *sub, void *par)
{
    int i;
    struct lock_profile_qhead *hq;
    struct lock_profile *eq;

    spin_lock(&lock_profile_lock);
    for ( i = 0; i < LOCKPROF_TYPE_N; i++ )
        for ( hq = lock_profile_ancs[i].head_q; hq; hq = hq->head_q )
            for ( eq = hq->elem_q; eq; eq = eq->next )
                sub(eq, i, hq->idx, par);
    spin_unlock(&lock_profile_lock);
}

static void cf_check spinlock_profile_print_elem(struct lock_profile *data,
    int32_t type, int32_t idx, void *par)
{
    unsigned int cpu;
    unsigned int lockval;

    if ( data->is_rlock )
    {
        cpu = data->ptr.rlock->debug.cpu;
        lockval = data->ptr.rlock->tickets.head_tail;
    }
    else
    {
        cpu = data->ptr.lock->debug.cpu;
        lockval = data->ptr.lock->tickets.head_tail;
    }

    printk("%s ", lock_profile_ancs[type].name);
    if ( type != LOCKPROF_TYPE_GLOBAL )
        printk("%d ", idx);
    printk("%s: addr=%p, lockval=%08x, ", data->name, data->ptr.lock, lockval);
    if ( cpu == SPINLOCK_NO_CPU )
        printk("not locked\n");
    else
        printk("cpu=%u\n", cpu);
    printk("  lock:%" PRIu64 "(%" PRI_stime "), block:%" PRIu64 "(%" PRI_stime ")\n",
           data->lock_cnt, data->time_hold, (uint64_t)data->block_cnt,
           data->time_block);
}

void cf_check spinlock_profile_printall(unsigned char key)
{
    s_time_t now = NOW();
    s_time_t diff;

    diff = now - lock_profile_start;
    printk("PRTOS lock profile info SHOW  (now = %"PRI_stime" total = "
           "%"PRI_stime")\n", now, diff);
    spinlock_profile_iterate(spinlock_profile_print_elem, NULL);
}

static void cf_check spinlock_profile_reset_elem(struct lock_profile *data,
    int32_t type, int32_t idx, void *par)
{
    data->lock_cnt = 0;
    data->block_cnt = 0;
    data->time_hold = 0;
    data->time_block = 0;
}

void cf_check spinlock_profile_reset(unsigned char key)
{
    s_time_t now = NOW();

    if ( key != '\0' )
        printk("PRTOS lock profile info RESET (now = %"PRI_stime")\n", now);
    lock_profile_start = now;
    spinlock_profile_iterate(spinlock_profile_reset_elem, NULL);
}

typedef struct {
    struct prtos_sysctl_lockprof_op *pc;
    int                      rc;
} spinlock_profile_ucopy_t;

static void cf_check spinlock_profile_ucopy_elem(struct lock_profile *data,
    int32_t type, int32_t idx, void *par)
{
    spinlock_profile_ucopy_t *p = par;
    struct prtos_sysctl_lockprof_data elem;

    if ( p->rc )
        return;

    if ( p->pc->nr_elem < p->pc->max_elem )
    {
        safe_strcpy(elem.name, data->name);
        elem.type = type;
        elem.idx = idx;
        elem.lock_cnt = data->lock_cnt;
        elem.block_cnt = data->block_cnt;
        elem.lock_time = data->time_hold;
        elem.block_time = data->time_block;
        if ( copy_to_guest_offset(p->pc->data, p->pc->nr_elem, &elem, 1) )
            p->rc = -EFAULT;
    }

    if ( !p->rc )
        p->pc->nr_elem++;
}

/* Dom0 control of lock profiling */
int spinlock_profile_control(struct prtos_sysctl_lockprof_op *pc)
{
    int rc = 0;
    spinlock_profile_ucopy_t par;

    switch ( pc->cmd )
    {
    case PRTOS_SYSCTL_LOCKPROF_reset:
        spinlock_profile_reset('\0');
        break;

    case PRTOS_SYSCTL_LOCKPROF_query:
        pc->nr_elem = 0;
        par.rc = 0;
        par.pc = pc;
        spinlock_profile_iterate(spinlock_profile_ucopy_elem, &par);
        pc->time = NOW() - lock_profile_start;
        rc = par.rc;
        break;

    default:
        rc = -EINVAL;
        break;
    }

    return rc;
}

void _lock_profile_register_struct(
    int32_t type, struct lock_profile_qhead *qhead, int32_t idx)
{
    qhead->idx = idx;
    spin_lock(&lock_profile_lock);
    qhead->head_q = lock_profile_ancs[type].head_q;
    lock_profile_ancs[type].head_q = qhead;
    spin_unlock(&lock_profile_lock);
}

void _lock_profile_deregister_struct(
    int32_t type, struct lock_profile_qhead *qhead)
{
    struct lock_profile_qhead **q;

    spin_lock(&lock_profile_lock);
    for ( q = &lock_profile_ancs[type].head_q; *q; q = &(*q)->head_q )
    {
        if ( *q == qhead )
        {
            *q = qhead->head_q;
            break;
        }
    }
    spin_unlock(&lock_profile_lock);
}

static int __init cf_check lock_prof_init(void)
{
    struct lock_profile **q;

    BUILD_BUG_ON(ARRAY_SIZE(lock_profile_ancs) != LOCKPROF_TYPE_N);

    for ( q = &__lock_profile_start; q < &__lock_profile_end; q++ )
    {
        (*q)->next = lock_profile_glb_q.elem_q;
        lock_profile_glb_q.elem_q = *q;

        if ( (*q)->is_rlock )
            (*q)->ptr.rlock->profile = *q;
        else
            (*q)->ptr.lock->profile = *q;
    }

    _lock_profile_register_struct(LOCKPROF_TYPE_GLOBAL,
                                  &lock_profile_glb_q, 0);

    return 0;
}
__initcall(lock_prof_init);

#endif /* CONFIG_DEBUG_LOCK_PROFILE */

/* === END INLINED: spinlock.c === */
/* tasklet.c compiled separately - cpu_callback/cpu_nfb conflicts */
/* === BEGIN INLINED: wait.c === */
#include <prtos_prtos_config.h>
/******************************************************************************
 * wait.c
 * 
 * Sleep in hypervisor context for some event to occur.
 * 
 * Copyright (c) 2010, Keir Fraser <keir@xen.org>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <prtos_sched.h>
#include <prtos_softirq.h>
#include <prtos_wait.h>
#include <prtos_errno.h>

struct waitqueue_vcpu {
    struct list_head list;
    struct vcpu *vcpu;
#ifdef CONFIG_X86
    /*
     * PRTOS/x86 does not have per-vcpu hypervisor stacks. So we must save the
     * hypervisor context before sleeping (descheduling), setjmp/longjmp-style.
     */
    void *esp;
    char *stack;
#endif
};

int init_waitqueue_vcpu(struct vcpu *v)
{
    struct waitqueue_vcpu *wqv;

    wqv = xzalloc(struct waitqueue_vcpu);
    if ( wqv == NULL )
        return -ENOMEM;

#ifdef CONFIG_X86
    wqv->stack = alloc_prtosheap_page();
    if ( wqv->stack == NULL )
    {
        xfree(wqv);
        return -ENOMEM;
    }
#endif

    INIT_LIST_HEAD(&wqv->list);
    wqv->vcpu = v;

    v->waitqueue_vcpu = wqv;

    return 0;
}

void destroy_waitqueue_vcpu(struct vcpu *v)
{
    struct waitqueue_vcpu *wqv;

    wqv = v->waitqueue_vcpu;
    if ( wqv == NULL )
        return;

    BUG_ON(!list_empty(&wqv->list));
#ifdef CONFIG_X86
    free_prtosheap_page(wqv->stack);
#endif
    xfree(wqv);

    v->waitqueue_vcpu = NULL;
}

void init_waitqueue_head(struct waitqueue_head *wq)
{
    spin_lock_init(&wq->lock);
    INIT_LIST_HEAD(&wq->list);
}

void destroy_waitqueue_head(struct waitqueue_head *wq)
{
    wake_up_all(wq);
}

void wake_up_nr(struct waitqueue_head *wq, unsigned int nr)
{
    struct waitqueue_vcpu *wqv;

    spin_lock(&wq->lock);

    while ( !list_empty(&wq->list) && nr-- )
    {
        wqv = list_entry(wq->list.next, struct waitqueue_vcpu, list);
        list_del_init(&wqv->list);
        vcpu_unpause(wqv->vcpu);
        put_domain(wqv->vcpu->domain);
    }

    spin_unlock(&wq->lock);
}


void wake_up_all(struct waitqueue_head *wq)
{
    wake_up_nr(wq, UINT_MAX);
}

#ifdef CONFIG_X86

static void __prepare_to_wait(struct waitqueue_vcpu *wqv)
{
    struct cpu_info *cpu_info = get_cpu_info();
    struct vcpu *curr = current;
    unsigned long dummy;

    ASSERT(wqv->esp == NULL);

    /* Save current VCPU affinity; force wakeup on *this* CPU only. */
    if ( vcpu_temporary_affinity(curr, smp_processor_id(), VCPU_AFFINITY_WAIT) )
    {
        gdprintk(PRTOSLOG_ERR, "Unable to set vcpu affinity\n");
        domain_crash(curr->domain);

        for ( ; ; )
            do_softirq();
    }

    /*
     * Hand-rolled setjmp().
     *
     * __prepare_to_wait() is the leaf of a deep calltree.  Preserve the GPRs,
     * bounds check what we want to stash in wqv->stack, copy the active stack
     * (up to cpu_info) into wqv->stack, then return normally.  Our caller
     * will shortly schedule() and discard the current context.
     *
     * The copy out is performed with a rep movsb.  When
     * check_wakeup_from_wait() longjmp()'s back into us, %rsp is pre-adjusted
     * to be suitable and %rsi/%rdi are swapped, so the rep movsb instead
     * copies in from wqv->stack over the active stack.
     */
    asm volatile (
        "push %%rbx; push %%rbp; push %%r12;"
        "push %%r13; push %%r14; push %%r15;"

        "sub %%esp,%%ecx;"
        "cmp %[sz], %%ecx;"
        "ja .L_skip;"       /* Bail if >4k */
        "mov %%rsp,%%rsi;"

        /* check_wakeup_from_wait() longjmp()'s to this point. */
        ".L_wq_resume: rep movsb;"
        "mov %%rsp,%%rsi;"

        ".L_skip:"
        "pop %%r15; pop %%r14; pop %%r13;"
        "pop %%r12; pop %%rbp; pop %%rbx"
        : "=&S" (wqv->esp), "=&c" (dummy), "=&D" (dummy)
        : "0" (0), "1" (cpu_info), "2" (wqv->stack),
          [sz] "i" (PAGE_SIZE)
        : "memory", "rax", "rdx", "r8", "r9", "r10", "r11" );

    if ( unlikely(wqv->esp == NULL) )
    {
        gdprintk(PRTOSLOG_ERR, "Stack too large in %s\n", __func__);
        domain_crash(curr->domain);

        for ( ; ; )
            do_softirq();
    }
}

static void __finish_wait(struct waitqueue_vcpu *wqv)
{
    wqv->esp = NULL;
    vcpu_temporary_affinity(current, NR_CPUS, VCPU_AFFINITY_WAIT);
}

void check_wakeup_from_wait(void)
{
    struct vcpu *curr = current;
    struct waitqueue_vcpu *wqv = curr->waitqueue_vcpu;

    ASSERT(list_empty(&wqv->list));

    if ( likely(wqv->esp == NULL) )
        return;

    /* Check if we are still pinned. */
    if ( unlikely(!(curr->affinity_broken & VCPU_AFFINITY_WAIT)) )
    {
        gdprintk(PRTOSLOG_ERR, "vcpu affinity lost\n");
        domain_crash(curr->domain);

        /* Re-initiate scheduler and don't longjmp(). */
        raise_softirq(SCHEDULE_SOFTIRQ);
        for ( ; ; )
            do_softirq();
    }

    /*
     * We are about to jump into a deeper call tree.  In principle, this risks
     * executing more RET than CALL instructions, and underflowing the RSB.
     *
     * However, we are pinned to the same CPU as previously.  Therefore,
     * either:
     *
     *   1) We've scheduled another vCPU in the meantime, and the context
     *      switch path has (by default) issued IBPB which flushes the RSB, or
     *
     *   2) We're still in the same context.  Returning back to the deeper
     *      call tree is resuming the execution path we left, and remains
     *      balanced as far as that logic is concerned.
     *
     *      In fact, the path through the scheduler will execute more CALL
     *      than RET instructions, making the RSB unbalanced in the safe
     *      direction.
     *
     * Therefore, no actions are necessary here to maintain RSB safety.
     */

    /*
     * Hand-rolled longjmp().
     *
     * check_wakeup_from_wait() is always called with a shallow stack,
     * immediately after the vCPU has been rescheduled.
     *
     * Adjust %rsp to be the correct depth for the (deeper) stack we want to
     * restore, then prepare %rsi, %rdi and %rcx such that when we rejoin the
     * rep movs in __prepare_to_wait(), it copies from wqv->stack over the
     * active stack.
     *
     * All other GPRs are available for use; They're restored from the stack,
     * or explicitly clobbered.
     */
    asm volatile ( "mov %%rdi, %%rsp;"
                   "jmp .L_wq_resume"
                   :
                   : "S" (wqv->stack), "D" (wqv->esp),
                     "c" ((char *)get_cpu_info() - (char *)wqv->esp)
                   : "memory" );
    unreachable();
}

#else /* !CONFIG_X86 */

#define __prepare_to_wait(wqv) ((void)0)
#define __finish_wait(wqv) ((void)0)

#endif

void prepare_to_wait(struct waitqueue_head *wq)
{
    struct vcpu *curr = current;
    struct waitqueue_vcpu *wqv = curr->waitqueue_vcpu;

    ASSERT_NOT_IN_ATOMIC();
    __prepare_to_wait(wqv);

    ASSERT(list_empty(&wqv->list));
    spin_lock(&wq->lock);
    list_add_tail(&wqv->list, &wq->list);
    vcpu_pause_nosync(curr);
    get_knownalive_domain(curr->domain);
    spin_unlock(&wq->lock);
}

void finish_wait(struct waitqueue_head *wq)
{
    struct vcpu *curr = current;
    struct waitqueue_vcpu *wqv = curr->waitqueue_vcpu;

    __finish_wait(wqv);

    if ( list_empty(&wqv->list) )
        return;

    spin_lock(&wq->lock);
    if ( !list_empty(&wqv->list) )
    {
        list_del_init(&wqv->list);
        vcpu_unpause(curr);
        put_domain(curr->domain);
    }
    spin_unlock(&wq->lock);
}

/* === END INLINED: wait.c === */
/* === BEGIN INLINED: xmalloc_tlsf.c === */
#include <prtos_prtos_config.h>
#include <prtos_prtos_config.h>
/*
 * Two Levels Segregate Fit memory allocator (TLSF)
 * Version 2.3.2
 *
 * Written by Miguel Masmano Tello <mimastel@doctor.upv.es>
 *
 * Thanks to Ismael Ripoll for his suggestions and reviews
 *
 * Copyright (C) 2007, 2006, 2005, 2004
 *
 * This code is released using a dual license strategy: GPL/LGPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of the GNU General Public License Version 2.0
 * Released under the terms of the GNU Lesser General Public License 
 * Version 2.1
 *
 * This is kernel port of TLSF allocator.
 * Original code can be found at: http://rtportal.upv.es/rtmalloc/
 * Adapted for Linux by Nitin Gupta (nitingupta910@gmail.com)
 * (http://code.google.com/p/compcache/source/browse/trunk/sub-projects
 *  /allocators/tlsf-kmod r229 dated Aug 27, 2008
 * Adapted for PRTOS by Dan Magenheimer (dan.magenheimer@oracle.com)
 */

#include <prtos_prtos_config.h>

#include <prtos_irq.h>
#include <prtos_mm.h>
#include <prtos_pfn.h>
#include <asm_time.h>
#include <asm_page.h>

#define MAX_POOL_NAME_LEN       16

/* Some IMPORTANT TLSF parameters */
#define MEM_ALIGN       (sizeof(void *) * 2)
#define MEM_ALIGN_MASK  (~(MEM_ALIGN - 1))

#define MAX_FLI         (30)
#define MAX_LOG2_SLI    (5)
#define MAX_SLI         (1 << MAX_LOG2_SLI)

#define FLI_OFFSET      (6)
/* tlsf structure just will manage blocks bigger than 128 bytes */
#define SMALL_BLOCK     (128)
#define REAL_FLI        (MAX_FLI - FLI_OFFSET)
#define MIN_BLOCK_SIZE  (sizeof(struct free_ptr))
#define BHDR_OVERHEAD   (sizeof(struct bhdr) - MIN_BLOCK_SIZE)

#define PTR_MASK        (sizeof(void *) - 1)
#define BLOCK_SIZE_MASK (0xFFFFFFFFU - PTR_MASK)

#define GET_NEXT_BLOCK(addr, r) ((struct bhdr *) \
                                ((char *)(addr) + (r)))
#define ROUNDUP_SIZE(r)         (((r) + MEM_ALIGN - 1) & MEM_ALIGN_MASK)
#define ROUNDDOWN_SIZE(r)       ((r) & MEM_ALIGN_MASK)
#define ROUNDUP_PAGE(r)         (((r) + PAGE_SIZE - 1) & PAGE_MASK)

#define BLOCK_STATE     (0x1)
#define PREV_STATE      (0x2)

/* bit 0 of the block size */
#define FREE_BLOCK      (0x1)
#define USED_BLOCK      (0x0)

/* bit 1 of the block size */
#define PREV_FREE       (0x2)
#define PREV_USED       (0x0)

static DEFINE_SPINLOCK(pool_list_lock);
static LIST_HEAD(pool_list_head);

struct free_ptr {
    struct bhdr *prev;
    struct bhdr *next;
};

struct bhdr {
    /* All blocks in a region are linked in order of physical address */
    struct bhdr *prev_hdr;
    /*
     * The size is stored in bytes
     *  bit 0: block is free, if set
     *  bit 1: previous block is free, if set
     */
    u32 size;
    /* Free blocks in individual freelists are linked */
    union {
        struct free_ptr free_ptr;
        u8 buffer[sizeof(struct free_ptr)];
    } ptr;
};

struct xmem_pool {
    /* First level bitmap (REAL_FLI bits) */
    u32 fl_bitmap;

    /* Second level bitmap */
    u32 sl_bitmap[REAL_FLI];

    /* Free lists */
    struct bhdr *matrix[REAL_FLI][MAX_SLI];

    spinlock_t lock;

    unsigned long max_size;
    unsigned long grow_size;

    /* Basic stats */
    unsigned long used_size;
    unsigned long num_regions;

    /* User provided functions for expanding/shrinking pool */
    xmem_pool_get_memory *get_mem;
    xmem_pool_put_memory *put_mem;

    struct list_head list;

    char name[MAX_POOL_NAME_LEN];
};

/*
 * Helping functions
 */

/**
 * Returns indexes (fl, sl) of the list used to serve request of size r
 */
static inline void MAPPING_SEARCH(unsigned long *r, int *fl, int *sl)
{
    int t;

    if ( *r < SMALL_BLOCK )
    {
        *fl = 0;
        *sl = *r / (SMALL_BLOCK / MAX_SLI);
    }
    else
    {
        t = (1 << (flsl(*r) - 1 - MAX_LOG2_SLI)) - 1;
        *r = *r + t;
        *fl = flsl(*r) - 1;
        *sl = (*r >> (*fl - MAX_LOG2_SLI)) - MAX_SLI;
        /* 
         * It's unclear what was the purpose of the commented-out code that now
         * is in the #else branch. The current form is motivated by the correction
         * of a violation MISRA:C 2012 Rule 3.1
         */
#if 1
        *fl -= FLI_OFFSET;
#else
        if ((*fl -= FLI_OFFSET) < 0) /* FL will be always >0! */
          *fl = *sl = 0;
#endif
        *r &= ~t;
    }
}

/**
 * Returns indexes (fl, sl) which is used as starting point to search
 * for a block of size r. It also rounds up requested size(r) to the
 * next list.
 */
static inline void MAPPING_INSERT(unsigned long r, int *fl, int *sl)
{
    if ( r < SMALL_BLOCK )
    {
        *fl = 0;
        *sl = r / (SMALL_BLOCK / MAX_SLI);
    }
    else
    {
        *fl = flsl(r) - 1;
        *sl = (r >> (*fl - MAX_LOG2_SLI)) - MAX_SLI;
        *fl -= FLI_OFFSET;
    }
}

/**
 * Returns first block from a list that hold blocks larger than or
 * equal to the one pointed by the indexes (fl, sl)
 */
static inline struct bhdr *FIND_SUITABLE_BLOCK(struct xmem_pool *p, int *fl,
                                               int *sl)
{
    u32 tmp = p->sl_bitmap[*fl] & (~0u << *sl);
    struct bhdr *b = NULL;

    if ( tmp )
    {
        *sl = ffs(tmp) - 1;
        b = p->matrix[*fl][*sl];
    }
    else
    {
        *fl = ffs(p->fl_bitmap & (~0u << (*fl + 1))) - 1;
        if ( likely(*fl > 0) )
        {
            *sl = ffs(p->sl_bitmap[*fl]) - 1;
            b = p->matrix[*fl][*sl];
        }
    }

    return b;
}

/**
 * Remove first free block(b) from free list with indexes (fl, sl).
 */
static inline void EXTRACT_BLOCK_HDR(struct bhdr *b, struct xmem_pool *p, int fl,
                                     int sl)
{
    p->matrix[fl][sl] = b->ptr.free_ptr.next;
    if ( p->matrix[fl][sl] )
    {
        p->matrix[fl][sl]->ptr.free_ptr.prev = NULL;
    }
    else
    {
        clear_bit(sl, &p->sl_bitmap[fl]);
        if ( !p->sl_bitmap[fl] )
            clear_bit(fl, &p->fl_bitmap);
    }
    b->ptr.free_ptr = (struct free_ptr) {NULL, NULL};
}

#define POISON_BYTE 0xAA

/**
 * Removes block(b) from free list with indexes (fl, sl)
 */
static inline void EXTRACT_BLOCK(struct bhdr *b, struct xmem_pool *p, int fl,
                                 int sl)
{
    if ( b->ptr.free_ptr.next )
        b->ptr.free_ptr.next->ptr.free_ptr.prev =
            b->ptr.free_ptr.prev;
    if ( b->ptr.free_ptr.prev )
        b->ptr.free_ptr.prev->ptr.free_ptr.next =
            b->ptr.free_ptr.next;
    if ( p->matrix[fl][sl] == b )
    {
        p->matrix[fl][sl] = b->ptr.free_ptr.next;
        if ( !p->matrix[fl][sl] )
        {
            clear_bit(sl, &p->sl_bitmap[fl]);
            if ( !p->sl_bitmap[fl] )
                clear_bit (fl, &p->fl_bitmap);
        }
    }
    b->ptr.free_ptr = (struct free_ptr) {NULL, NULL};

    if ( IS_ENABLED(CONFIG_XMEM_POOL_POISON) &&
         (b->size & BLOCK_SIZE_MASK) > MIN_BLOCK_SIZE &&
         memchr_inv(b->ptr.buffer + MIN_BLOCK_SIZE, POISON_BYTE,
                    (b->size & BLOCK_SIZE_MASK) - MIN_BLOCK_SIZE) )
    {
        printk(PRTOSLOG_ERR "XMEM Pool corruption found");
        BUG();
    }
}

/**
 * Insert block(b) in free list with indexes (fl, sl)
 */
static inline void INSERT_BLOCK(struct bhdr *b, struct xmem_pool *p, int fl, int sl)
{
    if ( IS_ENABLED(CONFIG_XMEM_POOL_POISON) &&
         (b->size & BLOCK_SIZE_MASK) > MIN_BLOCK_SIZE )
        memset(b->ptr.buffer + MIN_BLOCK_SIZE, POISON_BYTE,
               (b->size & BLOCK_SIZE_MASK) - MIN_BLOCK_SIZE);

    b->ptr.free_ptr = (struct free_ptr) {NULL, p->matrix[fl][sl]};
    if ( p->matrix[fl][sl] )
        p->matrix[fl][sl]->ptr.free_ptr.prev = b;
    p->matrix[fl][sl] = b;
    set_bit(sl, &p->sl_bitmap[fl]);
    set_bit(fl, &p->fl_bitmap);
}

/**
 * Region is a virtually contiguous memory region and Pool is
 * collection of such regions
 */
static inline void ADD_REGION(void *region, unsigned long region_size,
                              struct xmem_pool *pool)
{
    int fl, sl;
    struct bhdr *b, *lb;

    b = (struct bhdr *)(region);
    b->prev_hdr = NULL;
    b->size = ROUNDDOWN_SIZE(region_size - 2 * BHDR_OVERHEAD)
        | FREE_BLOCK | PREV_USED;
    MAPPING_INSERT(b->size & BLOCK_SIZE_MASK, &fl, &sl);
    INSERT_BLOCK(b, pool, fl, sl);
    /* The sentinel block: allows us to know when we're in the last block */
    lb = GET_NEXT_BLOCK(b->ptr.buffer, b->size & BLOCK_SIZE_MASK);
    lb->prev_hdr = b;
    lb->size = 0 | USED_BLOCK | PREV_FREE;
    pool->used_size += BHDR_OVERHEAD; /* only sentinel block is "used" */
    pool->num_regions++;
}

/*
 * TLSF pool-based allocator start.
 */

struct xmem_pool *xmem_pool_create(
    const char *name,
    xmem_pool_get_memory get_mem,
    xmem_pool_put_memory put_mem,
    unsigned long max_size,
    unsigned long grow_size)
{
    struct xmem_pool *pool;
    int pool_bytes, pool_order;

    BUG_ON(max_size && (max_size < grow_size));

    pool_bytes = ROUNDUP_SIZE(sizeof(*pool));
    pool_order = get_order_from_bytes(pool_bytes);

    pool = (void *)alloc_prtosheap_pages(pool_order, 0);
    if ( pool == NULL )
        return NULL;
    memset(pool, 0, pool_bytes);

    /* Round to next page boundary */
    max_size = ROUNDUP_PAGE(max_size);
    grow_size = ROUNDUP_PAGE(grow_size);

    /* pool global overhead not included in used size */
    pool->used_size = 0;

    pool->max_size = max_size;
    pool->grow_size = grow_size;
    pool->get_mem = get_mem;
    pool->put_mem = put_mem;
    strlcpy(pool->name, name, sizeof(pool->name));

    spin_lock_init(&pool->lock);

    spin_lock(&pool_list_lock);
    list_add_tail(&pool->list, &pool_list_head);
    spin_unlock(&pool_list_lock);

    return pool;
}




void *xmem_pool_alloc(unsigned long size, struct xmem_pool *pool)
{
    struct bhdr *b, *b2, *next_b, *region;
    int fl, sl;
    unsigned long tmp_size;

    ASSERT_ALLOC_CONTEXT();

    if ( size < MIN_BLOCK_SIZE )
        size = MIN_BLOCK_SIZE;
    else
    {
        tmp_size = ROUNDUP_SIZE(size);
        /* Guard against overflow. */
        if ( tmp_size < size )
            return NULL;
        size = tmp_size;
    }

    /* Rounding up the requested size and calculating fl and sl */

    spin_lock(&pool->lock);
 retry_find:
    MAPPING_SEARCH(&size, &fl, &sl);

    /* Searching a free block */
    if ( !(b = FIND_SUITABLE_BLOCK(pool, &fl, &sl)) )
    {
        /* Not found */
        if ( size > (pool->grow_size - 2 * BHDR_OVERHEAD) )
            goto out_locked;
        if ( pool->max_size && (pool->num_regions * pool->grow_size
                                > pool->max_size) )
            goto out_locked;
        spin_unlock(&pool->lock);
        if ( (region = pool->get_mem(pool->grow_size)) == NULL )
            goto out;
        spin_lock(&pool->lock);
        ADD_REGION(region, pool->grow_size, pool);
        goto retry_find;
    }
    EXTRACT_BLOCK_HDR(b, pool, fl, sl);

    /*-- found: */
    next_b = GET_NEXT_BLOCK(b->ptr.buffer, b->size & BLOCK_SIZE_MASK);
    /* Should the block be split? */
    tmp_size = (b->size & BLOCK_SIZE_MASK) - size;
    if ( tmp_size >= sizeof(struct bhdr) )
    {
        tmp_size -= BHDR_OVERHEAD;
        b2 = GET_NEXT_BLOCK(b->ptr.buffer, size);

        b2->size = tmp_size | FREE_BLOCK | PREV_USED;
        b2->prev_hdr = b;

        next_b->prev_hdr = b2;

        MAPPING_INSERT(tmp_size, &fl, &sl);
        INSERT_BLOCK(b2, pool, fl, sl);

        b->size = size | (b->size & PREV_STATE);
    }
    else
    {
        next_b->size &= (~PREV_FREE);
        b->size &= (~FREE_BLOCK); /* Now it's used */
    }

    pool->used_size += (b->size & BLOCK_SIZE_MASK) + BHDR_OVERHEAD;

    spin_unlock(&pool->lock);
    return (void *)b->ptr.buffer;

    /* Failed alloc */
 out_locked:
    spin_unlock(&pool->lock);

 out:
    return NULL;
}

void xmem_pool_free(void *ptr, struct xmem_pool *pool)
{
    struct bhdr *b, *tmp_b;
    int fl = 0, sl = 0;

    ASSERT_ALLOC_CONTEXT();

    if ( unlikely(ptr == NULL) )
        return;

    b = (struct bhdr *)((char *) ptr - BHDR_OVERHEAD);

    spin_lock(&pool->lock);
    b->size |= FREE_BLOCK;
    pool->used_size -= (b->size & BLOCK_SIZE_MASK) + BHDR_OVERHEAD;
    b->ptr.free_ptr = (struct free_ptr) { NULL, NULL};
    tmp_b = GET_NEXT_BLOCK(b->ptr.buffer, b->size & BLOCK_SIZE_MASK);
    if ( tmp_b->size & FREE_BLOCK )
    {
        MAPPING_INSERT(tmp_b->size & BLOCK_SIZE_MASK, &fl, &sl);
        EXTRACT_BLOCK(tmp_b, pool, fl, sl);
        b->size += (tmp_b->size & BLOCK_SIZE_MASK) + BHDR_OVERHEAD;
    }
    if ( b->size & PREV_FREE )
    {
        tmp_b = b->prev_hdr;
        MAPPING_INSERT(tmp_b->size & BLOCK_SIZE_MASK, &fl, &sl);
        EXTRACT_BLOCK(tmp_b, pool, fl, sl);
        tmp_b->size += (b->size & BLOCK_SIZE_MASK) + BHDR_OVERHEAD;
        b = tmp_b;
    }
    tmp_b = GET_NEXT_BLOCK(b->ptr.buffer, b->size & BLOCK_SIZE_MASK);
    tmp_b->prev_hdr = b;

    MAPPING_INSERT(b->size & BLOCK_SIZE_MASK, &fl, &sl);

    if ( (b->prev_hdr == NULL) && ((tmp_b->size & BLOCK_SIZE_MASK) == 0) )
    {
        pool->put_mem(b);
        pool->num_regions--;
        pool->used_size -= BHDR_OVERHEAD; /* sentinel block header */
        goto out;
    }

    INSERT_BLOCK(b, pool, fl, sl);

    tmp_b->size |= PREV_FREE;
    tmp_b->prev_hdr = b;
 out:
    spin_unlock(&pool->lock);
}


/*
 * Glue for xmalloc().
 */

static struct xmem_pool *prtospool;

static void *cf_check xmalloc_pool_get(unsigned long size)
{
    ASSERT(size == PAGE_SIZE);
    return alloc_prtosheap_page();
}

static void cf_check xmalloc_pool_put(void *p)
{
    free_prtosheap_page(p);
}

static void *xmalloc_whole_pages(unsigned long size, unsigned long align)
{
    unsigned int i, order;
    void *res, *p;

    order = get_order_from_bytes(max(align, size));

    res = alloc_prtosheap_pages(order, 0);
    if ( res == NULL )
        return NULL;

    for ( p = res + PAGE_ALIGN(size), i = 0; i < order; ++i )
        if ( (unsigned long)p & (PAGE_SIZE << i) )
        {
            free_prtosheap_pages(p, i);
            p += PAGE_SIZE << i;
        }

    PFN_ORDER(virt_to_page(res)) = PFN_UP(size);
    /* Check that there was no truncation: */
    ASSERT(PFN_ORDER(virt_to_page(res)) == PFN_UP(size));

    return res;
}

static void tlsf_init(void)
{
    prtospool = xmem_pool_create("xmalloc", xmalloc_pool_get,
                               xmalloc_pool_put, 0, PAGE_SIZE);
    BUG_ON(!prtospool);
}

/*
 * xmalloc()
 */

static void *strip_padding(void *p)
{
    const struct bhdr *b = p - BHDR_OVERHEAD;

    if ( b->size & FREE_BLOCK )
    {
        p -= b->size & ~FREE_BLOCK;
        b = p - BHDR_OVERHEAD;
        ASSERT(!(b->size & FREE_BLOCK));
    }

    return p;
}

static void *add_padding(void *p, unsigned long align)
{
    unsigned int pad;

    if ( (pad = -(long)p & (align - 1)) != 0 )
    {
        void *q = p + pad;
        struct bhdr *b = q - BHDR_OVERHEAD;

        ASSERT(q > p);
        b->size = pad | FREE_BLOCK;
        p = q;
    }

    return p;
}

void *_xmalloc(unsigned long size, unsigned long align)
{
    void *p = NULL;

    ASSERT_ALLOC_CONTEXT();

    if ( !size )
        return ZERO_BLOCK_PTR;

    ASSERT((align & (align - 1)) == 0);
    if ( align < MEM_ALIGN )
        align = MEM_ALIGN;
    size += align - MEM_ALIGN;

    /* Guard against overflow. */
    if ( size < align - MEM_ALIGN )
        return NULL;

    if ( !prtospool )
        tlsf_init();

    if ( size < PAGE_SIZE )
        p = xmem_pool_alloc(size, prtospool);
    if ( p == NULL )
        return xmalloc_whole_pages(size - align + MEM_ALIGN, align);

    /* Add alignment padding. */
    p = add_padding(p, align);

    ASSERT(((unsigned long)p & (align - 1)) == 0);
    return p;
}

void *_xzalloc(unsigned long size, unsigned long align)
{
    void *p = _xmalloc(size, align);

    return p ? memset(p, 0, size) : p;
}

void *_xrealloc(void *ptr, unsigned long size, unsigned long align)
{
    unsigned long curr_size;
    void *p;

    if ( !size )
    {
        xfree(ptr);
        return ZERO_BLOCK_PTR;
    }

    if ( ptr == NULL || ptr == ZERO_BLOCK_PTR )
        return _xmalloc(size, align);

    ASSERT(!(align & (align - 1)));
    if ( align < MEM_ALIGN )
        align = MEM_ALIGN;

    if ( !((unsigned long)ptr & (PAGE_SIZE - 1)) )
    {
        curr_size = (unsigned long)PFN_ORDER(virt_to_page(ptr)) << PAGE_SHIFT;

        if ( size <= curr_size && !((unsigned long)ptr & (align - 1)) )
            return ptr;
    }
    else
    {
        unsigned long tmp_size = size + align - MEM_ALIGN;
        const struct bhdr *b;

        /* Guard against overflow. */
        if ( tmp_size < size )
            return NULL;

        if ( tmp_size < PAGE_SIZE )
            tmp_size = (tmp_size < MIN_BLOCK_SIZE) ? MIN_BLOCK_SIZE :
                ROUNDUP_SIZE(tmp_size);

        /* Strip alignment padding. */
        p = strip_padding(ptr);

        b = p - BHDR_OVERHEAD;
        curr_size = b->size & BLOCK_SIZE_MASK;

        if ( tmp_size <= curr_size )
        {
            /* Add alignment padding. */
            p = add_padding(p, align);

            ASSERT(!((unsigned long)p & (align - 1)));

            return p;
        }
    }

    p = _xmalloc(size, align);
    if ( p )
    {
        memcpy(p, ptr, min(curr_size, size));
        xfree(ptr);
    }

    return p;
}

void xfree(void *p)
{
    ASSERT_ALLOC_CONTEXT();

    if ( p == NULL || p == ZERO_BLOCK_PTR )
        return;

    if ( !((unsigned long)p & (PAGE_SIZE - 1)) )
    {
        unsigned long size = PFN_ORDER(virt_to_page(p));
        unsigned int i, order = get_order_from_pages(size);

        BUG_ON((unsigned long)p & ((PAGE_SIZE << order) - 1));
        PFN_ORDER(virt_to_page(p)) = 0;
        for ( i = 0; ; ++i )
        {
            if ( !(size & (1 << i)) )
                continue;
            size -= 1 << i;
            free_prtosheap_pages(p + (size << PAGE_SHIFT), i);
            if ( i + 1 >= order )
                return;
        }
    }

    /* Strip alignment padding. */
    p = strip_padding(p);

    xmem_pool_free(p, prtospool);
}

/* === END INLINED: xmalloc_tlsf.c === */
