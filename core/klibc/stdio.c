/*
 * FILE: stdio.c
 *
 * Standard buffered input/output
 *
 * www.prtos.org
 */

#include <stdc.h>
#include <spinlock.h>
#include <objects/console.h>

#define SCRATCH 20
#define USE_LONG  // %lx, %Lu and so on, else only 16 bit
                  // %integer is allowed

#define USE_STRING   // %s, %S Strings as parameters
#define USE_CHAR     // %c, %C Chars as parameters
#define USE_INTEGER  // %i, %I Remove this format flag. %d, %D does the same
#define USE_HEX      // %x, %X Hexadecimal output

//#define USE_UPPERHEX	// %x, %X outputs A,B,C... else a,b,c...

#ifndef USE_HEX
#undef USE_UPPERHEX  // ;)
#endif

//#define USE_UPPER // uncommenting this removes %C,%D,%I,%O,%S,%U,%X and %L..
// only lowercase format flags are used
#define PADDING  // SPACE and ZERO padding

typedef struct {
    prtos_s32_t (*_putC)(prtos_s32_t c, void *a);
    void *a;
} fPrint_t;

static void __PrintFmt(fPrint_t *fp, const char *fmt, va_list args) {
    prtos_u8_t scratch[SCRATCH];
    prtos_u8_t fmtFlag;
    prtos_u16_t base;
    prtos_u8_t *ptr;
    prtos_u8_t issigned = 0;

#ifdef USE_LONG
    prtos_u8_t islong = 0;
    prtos_u8_t isvlong = 0;
    prtos_u64_t u_val = 0;
    prtos_s64_t s_val = 0;
#else
    prtos_u32_t u_val = 0;
    prtos_s32_t s_val = 0;
#endif

    prtos_u8_t fill;
    prtos_u8_t width;

    for (;;) {
        while ((fmtFlag = *(fmt++)) != '%') {  // Until '%' or '\0'
            if (!fmtFlag) {
                return;
            }
            fp->_putC(fmtFlag, fp->a);
        }

        issigned = 0;  // default unsigned
        base = 10;

        fmtFlag = *fmt++;  // get char after '%'

#ifdef PADDING
        width = 0;  // no formatting
        fill = 0;   // no formatting

        if (fmtFlag == '0' || fmtFlag == ' ') {  // SPACE or ZERO padding	?
            fill = fmtFlag;
            fmtFlag = *fmt++;  // get char after padding char
            while (fmtFlag >= '0' && fmtFlag <= '9') {
                width = 10 * width + (fmtFlag - '0');
                fmtFlag = *fmt++;  // get char after width char
            }
        }
#endif

#ifdef USE_LONG
        islong = 0;  // default int value
        isvlong = 0;
#ifdef USE_UPPER
        if (fmtFlag == 'l' || fmtFlag == 'L')  // Long value
#else
        if (fmtFlag == 'l')  // Long value
#endif
        {
            islong = 1;
            fmtFlag = *fmt++;  // get char after 'l' or 'L'
            if (fmtFlag == 'l') {
                isvlong = 1;
                fmtFlag = *fmt++;  // get char after 'l' or 'L'
            }
        }
#endif

        switch (fmtFlag) {
#ifdef USE_CHAR
            case 'c':
#ifdef USE_UPPER
            case 'C':
#endif
                fmtFlag = va_arg(args, prtos_s32_t);
                // no break -> run into default
#endif

            default:
                fp->_putC(fmtFlag, fp->a);
                continue;

#ifdef USE_STRING
#ifdef USE_UPPER
            case 'S':
#endif
            case 's':
                ptr = (prtos_u8_t *)va_arg(args, char *);
                while (*ptr) {
                    fp->_putC(*ptr, fp->a);
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
                fp->_putC('0', fp->a);
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

                if (issigned) {  // Signed types

#ifdef USE_LONG
                    if (isvlong) {
                        s_val = va_arg(args, prtos_s64_t);
                    } else if (islong) {
                        s_val = va_arg(args, prtos_s32_t);
                    } else {
                        s_val = va_arg(args, prtos_s32_t);
                    }
#else
                    s_val = va_arg(args, prtos_s32_t);
#endif

                    if (s_val < 0) {            // Value negativ ?
                        s_val = -s_val;         // Make it positiv
                        fp->_putC('-', fp->a);  // Output sign
                    }

                    if (!isvlong)
                        u_val = (prtos_u32_t)s_val;
                    else
                        u_val = (prtos_u64_t)s_val;
                } else {  // Unsigned types
#ifdef USE_LONG
                    if (isvlong) {
                        u_val = va_arg(args, prtos_u64_t);
                    } else if (islong) {
                        u_val = va_arg(args, prtos_u32_t);
                    } else {
                        u_val = va_arg(args, prtos_u32_t);
                    }
#else
                    u_val = va_arg(args, prtos_u32_t);
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
                    fp->_putC(*ptr, fp->a);
                    ptr++;
                }
        }
    }
}

static prtos_s32_t print_fputc(prtos_s32_t c, void *a) {
    prtos_u32_t *nc = (prtos_u32_t *)a;
    console_put_char(c);
    (*nc)++;
    return 1;
}

static spin_lock_t vpSpin = SPINLOCK_INIT;
prtos_s32_t vprintf(const char *fmt, va_list args) {
    prtos_s32_t nc = 0;
    fPrint_t fp = {print_fputc, (void *)&nc};
    prtos_word_t flags;

    spin_lock_irq_save(&vpSpin, flags);
    __PrintFmt(&fp, fmt, args);
    spin_unlockIrqRestore(&vpSpin, flags);
    return nc;
}

typedef struct sdata {
    char *s;
    prtos_s32_t *nc;
} sdata;

static prtos_s32_t Sprint_fputc(prtos_s32_t c, void *a) {
    sdata *sd = (sdata *)a;

    (*sd->s++) = c;
    (*sd->nc)++;
    return 1;
}

prtos_s32_t sprintf(char *s, char const *fmt, ...) {
    prtos_s32_t nc = 0;
    sdata sd = {s, &nc};
    fPrint_t fp = {Sprint_fputc, (void *)&sd};
    va_list args;
    prtos_word_t flags;

    spin_lock_irq_save(&vpSpin, flags);
    va_start(args, fmt);
    __PrintFmt(&fp, fmt, args);
    va_end(args);
    s[nc] = 0;
    spin_unlockIrqRestore(&vpSpin, flags);
    return nc;
}

typedef struct sndata {
    char *s;
    prtos_s32_t *n;  // s size
    prtos_s32_t *nc;
} sndata;

static prtos_s32_t SNprint_fputc(prtos_s32_t c, void *a) {
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

prtos_s32_t snprintf(char *s, prtos_s32_t n, const char *fmt, ...) {
    prtos_s32_t nc = 0;
    sndata snd = {s, &n, &nc};
    fPrint_t fp = {SNprint_fputc, (void *)&snd};
    va_list args;
    prtos_word_t flags;

    spin_lock_irq_save(&vpSpin, flags);
    va_start(args, fmt);
    __PrintFmt(&fp, fmt, args);
    va_end(args);
    spin_unlockIrqRestore(&vpSpin, flags);
    return nc;
}

prtos_s32_t kprintf(const char *format, ...) {
    va_list arg_ptr;
    prtos_s32_t n;
    // prtos_word_t flags;

    // hw_save_flags_cli(flags);
    va_start(arg_ptr, format);
    n = vprintf(format, arg_ptr);
    va_end(arg_ptr);
    // hw_restore_flags(flags);

    return n;
}

#ifdef CONFIG_EARLY_OUTPUT

static prtos_s32_t Earlyprint_fputc(prtos_s32_t c, void *a) {
    extern void early_put_char(prtos_u8_t c);
    prtos_u32_t *nc = (prtos_u32_t *)a;
    early_put_char(c);
    (*nc)++;
    return 1;
}

static prtos_s32_t EarlyVPrintf(const char *fmt, va_list args) {
    prtos_s32_t nc = 0;
    fPrint_t fp = {Earlyprint_fputc, (void *)&nc};
    prtos_word_t flags;

    spin_lock_irq_save(&vpSpin, flags);
    __PrintFmt(&fp, fmt, args);
    spin_unlockIrqRestore(&vpSpin, flags);
    return nc;
}

#endif

prtos_s32_t eprintf(const char *format, ...) {
#ifdef CONFIG_EARLY_OUTPUT
    va_list arg_ptr;
    prtos_s32_t n;

    va_start(arg_ptr, format);
    n = EarlyVPrintf(format, arg_ptr);
    va_end(arg_ptr);
    return n;
#else
    return 0;
#endif
}
