
#ifndef _RSW_STDC_H_
#define _RSW_STDC_H_

typedef __builtin_va_list va_list;

#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)

extern void *memcpy(void *dst, const void *src, unsigned long count);
extern void xputchar(prtos_s32_t c);
extern prtos_s32_t xprintf(const char *fmt, ...);

extern void init_output(void);

#endif
