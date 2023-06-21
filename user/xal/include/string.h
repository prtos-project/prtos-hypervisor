/*
 * FILE: string.h
 *
 * string functions
 *
 * www.prtos.org
 */

#ifndef _XAL_STRING_H_
#define _XAL_STRING_H_

extern void *memset(void *dst, prtos_s32_t s, prtos_u32_t count);
extern void *memcpy(void *dst, const void *src, prtos_u32_t count);
extern prtos_s32_t memcmp(const void *dst, const void *src, prtos_u32_t count);
extern char *strcpy(char *dst, const char *src);
extern char *strncpy(char *dest, const char *src, prtos_u_size_t n);
extern char *strcat(char *s, const char *t);
extern char *strncat(char *s, const char *t, prtos_u_size_t n);
extern prtos_s32_t strcmp(const char *s, const char *t);
extern prtos_s32_t strncmp(const char *s1, const char *s2, prtos_u_size_t n);
extern prtos_u32_t strlen(const char *s);
extern char *strrchr(const char *t, prtos_s32_t c);
extern char *strchr(const char *t, prtos_s32_t c);
extern char *strstr(const char *haystack, const char *needle);
extern void *memmove(void *dst, const void *src, prtos_u_size_t count);

#endif
