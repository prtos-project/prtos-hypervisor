/*
 * FILE: stdio.h
 *
 * stdio file
 *
 * www.prtos.org
 */

#ifndef _BAIL_STDIO_H_
#define _BAIL_STDIO_H_

#include <stdarg.h>
extern prtos_s32_t printf(const char *format, ...);
extern prtos_s32_t vprintf(const char *fmt, va_list args);
extern prtos_s32_t sprintf(char *s, char const *fmt, ...);
extern prtos_s32_t snprintf(char *s, prtos_s32_t n, const char *fmt, ...);
extern prtos_s32_t vsprintf(char *str, const char *format, va_list ap);

extern prtos_s32_t putchar(prtos_s32_t c);

#endif
