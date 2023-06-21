/*
 * std_c.c
 *
 * www.prtos.org
 */

#include <stdarg.h>
#include <string.h>
#include <prtos.h>

#define SCRATCH 20  // 32Bits go up to 4GB + 1 Byte for \0

// Spare some program space by making a comment of all not used format flag lines
#define USE_LONG  // %lx, %Lu and so on, else only 16 bit integer is allowed
//#define USE_OCTAL // %o, %O Octal output. Who needs this ?
#define USE_STRING    // %s, %S Strings as parameters
#define USE_CHAR      // %c, %C Chars as parameters
#define USE_INTEGER   // %i, %I Remove this format flag. %d, %D does the same
#define USE_HEX       // %x, %X Hexadezimal output
#define USE_UPPERHEX  // %x, %X outputs A,B,C... else a,b,c...
#ifndef USE_HEX
#undef USE_UPPERHEX  // ;)
#endif
#define USE_UPPER  // uncommenting this removes %C,%D,%I,%O,%S,%U,%X and %L..
// only lowercase format flags are used
#define PADDING  // SPACE and ZERO padding

prtos_s32_t atoi(const char *s) {
    long int v = 0;
    int sign = 1;
    unsigned base;

    while (*s == ' ' || (unsigned int)(*s - 9) < 5u) {
        s++;
    }

    switch (*s) {
        case '-':
            sign = -1;
        case '+':
            ++s;
    }

    base = 10;
    if (*s == '0') {
        if (*(s + 1) == 'x') { /* WARNING: assumes ascii. */
            s++;
            base = 16;
        } else {
            base = 8;
        }
        s++;
    }

    while ((unsigned int)(*s - '0') < base) {
        v = v * base + *s - '0';
        ++s;
    }

    if (sign == -1) {
        return -v;
    }
    return v;
}

char *strchr(const char *t, int c) {
    register char ch;

    ch = c;
    for (;;) {
        if (*t == ch) break;
        if (!*t) return 0;
        ++t;
    }
    return (char *)t;
}

char *strcat(char *s, const char *t) {
    char *dest = s;
    s += strlen(s);
    for (;;) {
        if (!(*s = *t)) break;
        ++s;
        ++t;
    }
    return dest;
}

char *strncat(char *s, const char *t, prtos_u_size_t n) {
    char *dest = s;
    register char *max;
    s += strlen(s);
    if ((max = s + n) == s) goto fini;
    for (;;) {
        if (!(*s = *t)) break;
        if (++s == max) break;
        ++t;
    }
    *s = 0;
fini:
    return dest;
}

void *memset(void *dst, int s, unsigned int count) {
    register char *a = dst;
    count++;
    while (--count) *a++ = s;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned int count) {
    register char *d = dst;
    register const char *s = src;
    ++count;
    while (--count) {
        *d = *s;
        ++d;
        ++s;
    }
    return dst;
}

char *strncpy(char *dst, const char *src, prtos_u_size_t n) {
    prtos_s32_t j;

    memset(dst, 0, n);

    for (j = 0; j < n && src[j]; j++) dst[j] = src[j];

    if (j >= n) dst[n - 1] = 0;

    return dst;
}

int strncmp(const char *s1, const char *s2, prtos_u_size_t n) {
    register const unsigned char *a = (const unsigned char *)s1;
    register const unsigned char *b = (const unsigned char *)s2;
    register const unsigned char *fini = a + n;

    while (a < fini) {
        register int res = *a - *b;
        if (res) return res;
        if (!*a) return 0;
        ++a;
        ++b;
    }
    return 0;
}
int strcmp(const char *s, const char *t) {
    char x;

    for (;;) {
        x = *s;
        if (x != *t) break;
        if (!x) break;
        ++s;
        ++t;
    }
    return ((int)x) - ((int)*t);
}

int memcmp(const void *dst, const void *src, unsigned int count) {
    int r;
    const char *d = dst;
    const char *s = src;
    ++count;
    while (--count) {
        if ((r = (*d - *s))) return r;
        ++d;
        ++s;
    }
    return 0;
}

unsigned int strlen(const char *s) {
    unsigned int i;
    if (!s) return 0;
    for (i = 0; *s; ++s) ++i;
    return i;
}

typedef struct file_print file_print;
struct file_print {
    int (*putc)(int c, void *a);
    void *a;
};

void vrprintf(file_print *fp, char const *fmt, va_list args) {
    unsigned char scratch[SCRATCH];
    unsigned char fmt_flag;
    unsigned short base;
    unsigned char *ptr;
    unsigned char issigned = 0;

#ifdef USE_LONG
    // #warning "use long"
    unsigned char islong = 0;
    unsigned char isvlong = 0;
    unsigned long long u_val = 0;
    long long s_val = 0;
#else
    unsigned int u_val = 0;
    int s_val = 0;
#endif

    unsigned char fill;
    unsigned char width;

    for (;;) {
        while ((fmt_flag = *(fmt++)) != '%') {  // Until '%' or '\0'
            if (!fmt_flag) {
                return;
            }
            if (fp->putc) fp->putc(fmt_flag, fp->a);
        }

        issigned = 0;  // default unsigned
        base = 10;

        fmt_flag = *fmt++;  // get char after '%'

#ifdef PADDING
        width = 0;  // no formatting
        fill = 0;   // no formatting

        if (fmt_flag == '0' || fmt_flag == ' ')  // SPACE or ZERO padding	?
        {
            fill = fmt_flag;
            fmt_flag = *fmt++;  // get char after padding char
            while (fmt_flag >= '0' && fmt_flag <= '9') {
                width = 10 * width + (fmt_flag - '0');
                fmt_flag = *fmt++;  // get char after width char
            }
        }
#endif

#ifdef USE_LONG
        islong = 0;  // default int value
        isvlong = 0;
#ifdef USE_UPPER
        if (fmt_flag == 'l' || fmt_flag == 'L')  // Long value
#else
        if (fmt_flag == 'l')  // Long value
#endif
        {
            islong = 1;
            fmt_flag = *fmt++;  // get char after 'l' or 'L'
            if (fmt_flag == 'l') {
                isvlong = 1;
                fmt_flag = *fmt++;  // get char after 'l' or 'L'
            }
        }
#endif

        switch (fmt_flag) {
#ifdef USE_CHAR
            case 'c':
#ifdef USE_UPPER
            case 'C':
#endif
                fmt_flag = va_arg(args, int);
                // no break -> run into default
#endif

            default:
                if (fp->putc) fp->putc(fmt_flag, fp->a);
                continue;

#ifdef USE_STRING
#ifdef USE_UPPER
            case 'S':
#endif
            case 's':
                ptr = (unsigned char *)va_arg(args, char *);
                while (*ptr) {
                    if (fp->putc) fp->putc(*ptr, fp->a);
                    ptr++;
                }
                continue;
#endif

#ifdef USE_OCTAL
            case 'o':
#ifdef USE_UPPER
            case 'O':
#endif
                base = 8;
                if (fp->putc) fp->putc('0', fp->a);
                goto CONVERSION_LOOP;
#endif

#ifdef USE_INTEGER  // don't use %i, is same as %d
            case 'i':
#ifdef USE_UPPER
            case 'I':
#endif
#endif
            case 'd':
#ifdef USE_UPPER
            case 'D':
#endif
                issigned = 1;
            // no break -> run into next case
            case 'u':
#ifdef USE_UPPER
            case 'U':
#endif

// don't insert some case below this if USE_HEX is undefined !
// or put			 goto CONVERSION_LOOP;	before next case.
#ifdef USE_HEX
                goto CONVERSION_LOOP;
            case 'x':
#ifdef USE_UPPER
            case 'X':
#endif
                base = 16;
#endif

            CONVERSION_LOOP:

                if (issigned)  // Signed types
                {
#ifdef USE_LONG
                    if (isvlong) {
                        s_val = va_arg(args, long long);
                    } else if (islong) {
                        s_val = va_arg(args, long);
                    } else {
                        s_val = va_arg(args, int);
                    }
#else
                    s_val = va_arg(args, int);
#endif

                    if (s_val < 0)  // Value negativ ?
                    {
                        s_val = -s_val;                      // Make it positiv
                        if (fp->putc) fp->putc('-', fp->a);  // Output sign
                    }

                    if (!isvlong)
                        u_val = (unsigned long)s_val;
                    else
                        u_val = (unsigned long long)s_val;
                } else  // Unsigned types
                {
#ifdef USE_LONG
                    if (isvlong) {
                        u_val = va_arg(args, unsigned long long);
                    } else if (islong) {
                        u_val = va_arg(args, unsigned long);
                    } else {
                        u_val = va_arg(args, unsigned int);
                    }
#else
                    u_val = va_arg(args, unsigned int);
#endif
                }

                ptr = scratch + SCRATCH;
                *--ptr = 0;
                do {
                    char ch = u_val % base + '0';
#ifdef USE_HEX
                    if (ch > '9') {
                        ch += 'a' - '9' - 1;
#ifdef USE_UPPERHEX
                        ch -= 0x20;
#endif
                    }
#endif
                    *--ptr = ch;
                    u_val /= base;

#ifdef PADDING
                    if (width) width--;  // calculate number of padding chars
#endif
                } while (u_val);

#ifdef PADDING
                while (width--) *--ptr = fill;  // insert padding chars
#endif

                while (*ptr) {
                    if (fp->putc) fp->putc(*ptr, fp->a);
                    ptr++;
                }
        }
    }
}

int putchar(int c) {
    static char buff[512];
    static int i = 0;
    if (i > (sizeof(buff) - 2)) {
        prtos_write_console(&buff[0], i);
        i = 0;
    }
    if (c) buff[i++] = c;

    if (c == '\n' || c == '\r') {
        if (i) prtos_write_console(&buff[0], i);
        i = 0;
    }
    return c;
}

static int printfputc(int c, void *a) {
    unsigned long *nc = (unsigned long *)a;
    // if(c == '\n'){
    // putchar('\r');
    //(*nc)++;
    //}

    putchar(c);
    (*nc)++;
    return 1;
}

int printf(char const *fmt, ...) {
    int nc = 0;
    file_print fp = {printfputc, (void *)&nc};
    va_list args;
    va_start(args, fmt);
    vrprintf(&fp, fmt, args);
    va_end(args);

    return nc;
}

typedef struct sdata sdata;
struct sdata {
    char *s;
    int *nc;
};

static int sprintfputc(int c, void *a) {
    sdata *sd = (sdata *)a;

    (*sd->s++) = c;
    (*sd->nc)++;
    return 1;
}

int sprintf(char *s, char const *fmt, ...) {
    int nc = 0;
    sdata sd = {s, &nc};
    file_print fp = {sprintfputc, (void *)&sd};
    va_list args;

    va_start(args, fmt);
    vrprintf(&fp, fmt, args);
    va_end(args);
    return nc;
}

typedef struct sndata sndata;
struct sndata {
    char *s;
    int *n;  // s size
    int *nc;
};

static int snprintfputc(int c, void *a) {
    sndata *snd = (sndata *)a;

    if (*snd->n > *snd->nc) {
        if (snd->s) {
            snd->s[(*snd->nc)] = c;
            (*snd->nc)++;
            snd->s[(*snd->nc)] = '\0';
        }
    }
    return 1;
}

int snprintf(char *s, int n, const char *fmt, ...) {
    int nc = 0;
    sndata snd = {s, &n, &nc};
    file_print fp = {snprintfputc, (void *)&snd};
    va_list args;

    va_start(args, fmt);
    vrprintf(&fp, fmt, args);
    va_end(args);
    return nc;
}

#define MAX_VSPRINTF_SIZE 1024

int vsprintf(char *str, const char *format, va_list ap) {
    int nc = 0;
    int maxNc = MAX_VSPRINTF_SIZE;

    sndata snd = {str, &maxNc, &nc};
    file_print fp = {snprintfputc, (void *)&snd};
    vrprintf(&fp, format, ap);
    return nc;
}
