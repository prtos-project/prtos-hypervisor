/*
 * FILE: stddef.h
 *
 * stddef file
 *
 * www.prtos.org
 */

#ifndef _BAIL_STDDEF_H_
#define _BAIL_STDDEF_H_

#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(_type, _member) __compiler_offsetof(_type, _member)
#else
#define offsetof(_type, _member) ((prtos_u_size_t) & ((_type *)0)->_member)
#endif

#endif
