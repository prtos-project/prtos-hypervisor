/*
 * FILE: linkage.h
 *
 * Definition of some macros to ease the interoperatibility between
 * assembly and C
 *
 * www.prtos.org
 */

#ifndef _PRTOS_ARCH_LINKAGE_H_
#define _PRTOS_ARCH_LINKAGE_H_

#ifndef _ASSEMBLY_
#define ALIGNMENT 8
#define ASM_ALIGN .align ALIGNMENT
#define __stdcall 
#define __hypercall 
#define ALIGNED_C 
#endif


#ifdef __ASSEMBLY__
// #include <xen/macros.h>


#define ROUNDUP(x, a) (((x) + (a) - 1) & ~((a) - 1))

#define IS_ALIGNED(val, align) (!((val) & ((align) - 1)))

#define DIV_ROUND(n, d) (((n) + (d) / 2) / (d))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/*
 * Given an unsigned integer argument, expands to a mask where just the least
 * significant nonzero bit of the argument is set, or 0 if no bits are set.
 */
#define ISOLATE_LSB(x) ((x) & -(x))

#define MASK_EXTR(v, m) (((v) & (m)) / ISOLATE_LSB(m))
#define MASK_INSR(v, m) (((v) * ISOLATE_LSB(m)) & (m))

#define count_args_(dot, a1, a2, a3, a4, a5, a6, a7, a8, x, ...) x
#define count_args(args...) \
    count_args_(., ## args, 8, 7, 6, 5, 4, 3, 2, 1, 0)

#define ARG1_(x, y...) (x)
#define ARG2_(x, y...) ARG1_(y)
#define ARG3_(x, y...) ARG2_(y)
#define ARG4_(x, y...) ARG3_(y)

#define ARG__(nr) ARG ## nr ## _
#define ARG_(nr)  ARG__(nr)
#define LASTARG(x, y...) ARG_(count_args(x, ## y))(x, ## y)

/* Indirect macros required for expanded argument pasting. */
#define PASTE_(a, b) a ## b
#define PASTE(a, b) PASTE_(a, b)

#define __STR(...) #__VA_ARGS__
#define STR(...) __STR(__VA_ARGS__)



#ifndef CODE_FILL
# define CODE_FILL ~0
#endif

#ifndef DATA_ALIGN
# define DATA_ALIGN 0
#endif
#ifndef DATA_FILL
# define DATA_FILL ~0
#endif

#define SYM_ALIGN(align...) .balign align

#define SYM_L_GLOBAL(name) .globl name; .hidden name
#define SYM_L_WEAK(name)   .weak name
#define SYM_L_LOCAL(name)  /* nothing */

#define SYM_T_FUNC         STT_FUNC
#define SYM_T_DATA         STT_OBJECT
#define SYM_T_NONE         STT_NOTYPE

#define SYM(name, typ, linkage, align...)         \
        .type name, SYM_T_ ## typ;                \
        SYM_L_ ## linkage(name);                  \
        SYM_ALIGN(align);                         \
        name:

#define END(name) .size name, . - name

/*
 * CODE_FILL in particular may need to expand to nothing (e.g. for RISC-V), in
 * which case we also need to get rid of the comma in the .balign directive.
 */
#define count_args_exp(args...) count_args(args)
#if count_args_exp(CODE_FILL)
# define DO_CODE_ALIGN(align...) LASTARG(CONFIG_FUNCTION_ALIGNMENT, ## align), \
                                 CODE_FILL
#else
# define DO_CODE_ALIGN(align...) LASTARG(CONFIG_FUNCTION_ALIGNMENT, ## align)
#endif

#define FUNC(name, align...) \
        SYM(name, FUNC, GLOBAL, DO_CODE_ALIGN(align))
#define LABEL(name, align...) \
        SYM(name, NONE, GLOBAL, DO_CODE_ALIGN(align))
#define DATA(name, align...) \
        SYM(name, DATA, GLOBAL, LASTARG(DATA_ALIGN, ## align), DATA_FILL)

#define FUNC_LOCAL(name, align...) \
        SYM(name, FUNC, LOCAL, DO_CODE_ALIGN(align))
#define LABEL_LOCAL(name, align...) \
        SYM(name, NONE, LOCAL, DO_CODE_ALIGN(align))
#define DATA_LOCAL(name, align...) \
        SYM(name, DATA, LOCAL, LASTARG(DATA_ALIGN, ## align), DATA_FILL)

#define ASM_INT(label, val)    DATA(label, 4) .long (val); END(label)

#endif /*  __ASSEMBLY__ */


#endif
