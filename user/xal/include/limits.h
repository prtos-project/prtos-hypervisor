/*
 * FILE: limits.h
 *
 * limits
 *
 * www.prtos.org
 */

#ifndef _XAL_LIMITS_H_
#define _XAL_LIMITS_H_

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
#if __WORDSIZE == 64
#define __LONG_MAX__ 9223372036854775807L
#else
#define __LONG_MAX__ 2147483647L
#endif
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

#endif
