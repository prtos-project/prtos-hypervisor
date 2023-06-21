/*
 * FILE: prtospack
 *
 * Create a pack holding the image of prtos and partitions to be written
 * into the ROM
 *
 * www.prtos.org
 */

#ifndef _PRTOS_PACK_H_
#define _PRTOS_PACK_H_
#define TOOL_NAME "prtospack"
#define DEFAULT_OUTPUT "a.out"

#define USAGE                                                             \
    "Usage: " TOOL_NAME " [list] [build]\n"                               \
    "\tlist [-c] <container>\n"                                           \
    "\tbuild -h <prtos_file>[@<offset>]:<conf_file>[@<offset>] "          \
    "[-p <id>:<partition_file>[@<offset>][:<custom_file>[@<offset>]*]+\n" \
    "\tcheck <xml.xef> [[-h <prtos_core.xef>[:<custom.xef>]*]|[-p <id>:<partition.xef>[:<custom.xef>]*]]+\n"

#define DIV_ROUNDUP(a, b) ((!(a % b)) ? a / b : (a / b) + 1)
#define ALIGN_SIZE 8
#define ALIGNTO(x, s) ((((~(x)) + 1) & ((s)-1)) + (x))

#ifdef _DEBUG_
#define STR(s) #s
#define ESTR(s) STR(s)
#define FILENO "[" __BASE_FILE__ ":" ESTR(__LINE__) "]"
#else
#define FILENO ""
#endif

#define DO_MALLOC(p, s)                                        \
    do {                                                       \
        if (!(p = malloc(s))) {                                \
            error_printf("Memory pool out of memory " FILENO); \
        }                                                      \
    } while (0)

#define DO_REALLOC(p, s)                                       \
    do {                                                       \
        if (!(p = realloc(p, s))) {                            \
            error_printf("Memory pool out of memory " FILENO); \
        }                                                      \
    } while (0)

#define DO_WRITE(fd, b, s)                                    \
    do {                                                      \
        if (write(fd, b, s) != s) {                           \
            error_printf("Error writting container " FILENO); \
        }                                                     \
    } while (0)

#define DO_READ(fd, b, s)                                    \
    do {                                                     \
        if (read(fd, b, s) != s) {                           \
            error_printf("Error reading from file " FILENO); \
        }                                                    \
    } while (0)

#undef OFFSETOF
#ifdef __compiler_offsetof
#define OFFSETOF(_type, _member) __compiler_offsetof(_type, _member)
#else
#define OFFSETOF(_type, _member) ((long)&((_type *)0)->_member)
#endif

extern void error_printf(char *fmt, ...);

#endif
