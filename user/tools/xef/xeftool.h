/*
 * FILE: xef.h
 *
 * www.prtos.org
 */

#ifndef _XEF_H_
#define _XEF_H_

#include <stdlib.h>
#include <endianess.h>

#define DIV_ROUNDUP(a, b) ((!(a % b)) ? a / b : (a / b) + 1)
#define ALIGN_SIZE 8
#define ALIGNTO(x, s) ((((~(x)) + 1) & ((s)-1)) + (x))

#define DO_REALLOC(p, s)                               \
    do {                                               \
        if (!(p = realloc(p, s))) {                    \
            error_printf("Memory pool out of memory"); \
        }                                              \
    } while (0)

#define DO_MALLOC(p, s)                                \
    do {                                               \
        if (!(p = malloc(s))) {                        \
            error_printf("Memory pool out of memory"); \
        }                                              \
    } while (0)

#define DO_WRITE(fd, b, s)                          \
    do {                                            \
        if (write(fd, b, s) != s) {                 \
            error_printf("Error writting to file"); \
        }                                           \
    } while (0)

#define DO_READ(fd, b, s)                            \
    do {                                             \
        if (read(fd, b, s) != s) {                   \
            error_printf("Error reading from file"); \
        }                                            \
    } while (0)

extern void error_printf(char *fmt, ...);

#endif
