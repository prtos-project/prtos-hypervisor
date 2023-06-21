/*
 * FILE: stdlib.h
 *
 * standard library functions
 *
 * www.prtos.org
 */

#ifndef _XAL_STDLIB_H_
#define _XAL_STDLIB_H_

#define NULL ((void *)0)

extern prtos_s32_t atoi(const char *s);
extern prtos_u32_t strtoul(const char *ptr, char **endptr, prtos_s32_t base);
extern prtos_s32_t strtol(const char *nptr, char **endptr, prtos_s32_t base);
extern prtos_u64_t strtoull(const char *ptr, char **endptr, prtos_s32_t base);
extern prtos_s64_t strtoll(const char *nptr, char **endptr, prtos_s32_t base);
extern char *basename(char *path);
extern void exit(prtos_s32_t status);

#endif
