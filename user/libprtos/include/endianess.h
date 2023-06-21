/*
 * FILE: endianess.h
 *
 * endianess handling functions
 *
 * www.prtos.org
 */

#ifndef _ENDIANESS_H_
#define _ENDIANESS_H_

#ifdef HOST
#include <sys/param.h>
#endif

#if (((__BYTE_ORDER == __LITTLE_ENDIAN) && defined(CONFIG_TARGET_LITTLE_ENDIAN)) || ((__BYTE_ORDER == __BIG_ENDIAN) && defined(CONFIG_TARGET_BIG_ENDIAN)))
#define RWORD(i) (i)
#define RHALF(i) (i)
#define RDWORD(i) (i)
#else
#define RWORD(i) ((((i)&0xff) << 24) + (((i)&0xff00) << 8) + (((i)&0xff0000) >> 8) + (((i) >> 24) & 0xff))
#define RHALF(i) ((((i)&0xff) << 8) + (((i)&0xff00) >> 8))
#define RDWORD(i) ((prtos_u64_t)RWORD((prtos_u32_t)i) << 32) | RWORD((prtos_u32_t)(i >> 32))

#endif

#endif
