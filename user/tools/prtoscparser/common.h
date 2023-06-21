/*
 * FILE: common.h
 *
 * Common definitions
 *
 * www.prtos.org
 */

#ifndef _COMMON_H_
#define _COMMON_H_

#include "stdio.h"

#define TAB "    "
#define ADD1TAB(x) TAB x
#define ADD2TAB(x) TAB ADD1TAB(x)
#define ADD3TAB(x) TAB ADD2TAB(x)
#define ADD4TAB(x) TAB ADD3TAB(x)
#define ADD5TAB(x) TAB ADD4TAB(x)
#define ADD6TAB(x) TAB ADD5TAB(x)
#define ADD7TAB(x) TAB ADD6TAB(x)
#define ADD8TAB(x) TAB ADD7TAB(x)
#define ADD9TAB(x) TAB ADD8TAB(x)
#define ADD10TAB(x) TAB ADD9TAB(x)
/* ADDNTAB(n,x), where n is a digit and x a string */
#define ADDNTAB(n, x) ADD##n##TAB(x)

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

#define DO_MALLOCZ(p, s)                               \
    do {                                               \
        if (!(p = malloc(s))) {                        \
            error_printf("Memory pool out of memory"); \
        }                                              \
        memset(p, 0, s);                               \
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

extern void line_error(int line_number, char *fmt, ...);
extern void error_printf(char *fmt, ...);
extern void generate_c_file(FILE *out_file);
extern void exec_xml_conf_build(char *path, char *in, char *out);
extern void calc_digest(char *out);
extern char *in_file;
extern void setup_hw_irq_mask(void);

//#define ALIGN(size, align) ((((~(size))+1)&((align)-1))+(size))
extern void rsv_block(unsigned int size, int align, char *comment);
extern void print_blocks(FILE *out_file);

struct vcpu_to_cpu {
    int line;
    prtos_s32_t cpu;
};

extern struct vcpu_to_cpu **vcpu_to_cpu_table;

#define TAGGED_MEM_BLOCK(tag, size, align, comment)                 \
    do {                                                            \
        if (size) {                                                 \
            fprintf(out_file,                                       \
                    "\n__asm__ (/* %s */ \\\n"                      \
                    "         \".section .bss.mempool\\n\\t\" \\\n" \
                    "         \".align %d\\n\\t\" \\\n"             \
                    "         \"%s:.zero %d\\n\\t\" \\\n"           \
                    "         \".previous\\n\\t\");\n",             \
                    comment, align, tag, size);                     \
        }                                                           \
    } while (0)

#endif
