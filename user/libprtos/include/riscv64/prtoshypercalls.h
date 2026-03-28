/*
 * FILE: prtoshypercalls.h
 *
 * Arch hypercalls definition for RISC-V 64
 *
 * www.prtos.org
 */

#ifndef _ARCH_LIB_PRTOS_HYPERCALLS_H_
#define _ARCH_LIB_PRTOS_HYPERCALLS_H_

#ifdef _PRTOS_KERNEL_
#error Guest file, do not include.
#endif

#include <prtos_inc/config.h>
#include <prtos_inc/arch/prtos_def.h>
#include <prtos_inc/arch/processor.h>

#include <prtos_inc/linkage.h>

#ifndef __ASSEMBLY__

#define __prtos_cli() do {} while (0)
#define __prtos_sti() do {} while (0)

#define __DO_PRTOSHC "ecall"

#define _PRTOS_HCALL0(_hc_nr, _r)                                          \
    do {                                                                    \
        register prtos_u64_t _a0 __asm__("a0") = (prtos_u64_t)(_hc_nr);   \
        __asm__ __volatile__("ecall"                                        \
            : "+r"(_a0)                                                     \
            :                                                               \
            : "a1", "a2", "a3", "a4", "a5", "memory");                    \
        (_r) = (prtos_s32_t)_a0;                                           \
    } while (0)

#define _PRTOS_HCALL1(_a0_val, _hc_nr, _r)                                 \
    do {                                                                    \
        register prtos_u64_t _a0 __asm__("a0") = (prtos_u64_t)(_hc_nr);   \
        register prtos_u64_t _a1 __asm__("a1") = (prtos_u64_t)(_a0_val);  \
        __asm__ __volatile__("ecall"                                        \
            : "+r"(_a0)                                                     \
            : "r"(_a1)                                                      \
            : "a2", "a3", "a4", "a5", "memory");                          \
        (_r) = (prtos_s32_t)_a0;                                           \
    } while (0)

#define _PRTOS_HCALL2(_a0_val, _a1_val, _hc_nr, _r)                        \
    do {                                                                    \
        register prtos_u64_t _a0 __asm__("a0") = (prtos_u64_t)(_hc_nr);   \
        register prtos_u64_t _a1 __asm__("a1") = (prtos_u64_t)(_a0_val);  \
        register prtos_u64_t _a2 __asm__("a2") = (prtos_u64_t)(_a1_val);  \
        __asm__ __volatile__("ecall"                                        \
            : "+r"(_a0)                                                     \
            : "r"(_a1), "r"(_a2)                                           \
            : "a3", "a4", "a5", "memory");                                \
        (_r) = (prtos_s32_t)_a0;                                           \
    } while (0)

#define _PRTOS_HCALL3(_a0_val, _a1_val, _a2_val, _hc_nr, _r)               \
    do {                                                                    \
        register prtos_u64_t _a0 __asm__("a0") = (prtos_u64_t)(_hc_nr);   \
        register prtos_u64_t _a1 __asm__("a1") = (prtos_u64_t)(_a0_val);  \
        register prtos_u64_t _a2 __asm__("a2") = (prtos_u64_t)(_a1_val);  \
        register prtos_u64_t _a3 __asm__("a3") = (prtos_u64_t)(_a2_val);  \
        __asm__ __volatile__("ecall"                                        \
            : "+r"(_a0)                                                     \
            : "r"(_a1), "r"(_a2), "r"(_a3)                                \
            : "a4", "a5", "memory");                                      \
        (_r) = (prtos_s32_t)_a0;                                           \
    } while (0)

#define _PRTOS_HCALL4(_a0_val, _a1_val, _a2_val, _a3_val, _hc_nr, _r)      \
    do {                                                                    \
        register prtos_u64_t _a0 __asm__("a0") = (prtos_u64_t)(_hc_nr);   \
        register prtos_u64_t _a1 __asm__("a1") = (prtos_u64_t)(_a0_val);  \
        register prtos_u64_t _a2 __asm__("a2") = (prtos_u64_t)(_a1_val);  \
        register prtos_u64_t _a3 __asm__("a3") = (prtos_u64_t)(_a2_val);  \
        register prtos_u64_t _a4 __asm__("a4") = (prtos_u64_t)(_a3_val);  \
        __asm__ __volatile__("ecall"                                        \
            : "+r"(_a0)                                                     \
            : "r"(_a1), "r"(_a2), "r"(_a3), "r"(_a4)                     \
            : "memory");                                                  \
        (_r) = (prtos_s32_t)_a0;                                           \
    } while (0)

#define _PRTOS_HCALL5(_a0_val, _a1_val, _a2_val, _a3_val, _a4_val, _hc_nr, _r) \
    do {                                                                    \
        register prtos_u64_t _a0 __asm__("a0") = (prtos_u64_t)(_hc_nr);   \
        register prtos_u64_t _a1 __asm__("a1") = (prtos_u64_t)(_a0_val);  \
        register prtos_u64_t _a2 __asm__("a2") = (prtos_u64_t)(_a1_val);  \
        register prtos_u64_t _a3 __asm__("a3") = (prtos_u64_t)(_a2_val);  \
        register prtos_u64_t _a4 __asm__("a4") = (prtos_u64_t)(_a3_val);  \
        register prtos_u64_t _a5 __asm__("a5") = (prtos_u64_t)(_a4_val);  \
        __asm__ __volatile__("ecall"                                        \
            : "+r"(_a0)                                                     \
            : "r"(_a1), "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5)          \
            : );                                                          \
        (_r) = (prtos_s32_t)_a0;                                           \
    } while (0)

#define prtos_hcall0(_hc)                        \
    __stdcall void prtos_##_hc(void) {           \
        prtos_s32_t _r;                          \
        if (prtos_flush_hyp_batch() < 0) return; \
        _PRTOS_HCALL0(_hc##_nr, _r);             \
    }

#define prtos_hcall0r(_hc)                                 \
    __stdcall prtos_s32_t prtos_##_hc(void) {              \
        prtos_s32_t _r;                                    \
        if ((_r = prtos_flush_hyp_batch()) < 0) return _r; \
        _PRTOS_HCALL0(_hc##_nr, _r);                       \
        return _r;                                         \
    }

#define prtos_hcall1(_hc, _t0, _a0)              \
    __stdcall void prtos_##_hc(_t0 _a0) {        \
        prtos_s32_t _r;                          \
        if (prtos_flush_hyp_batch() < 0) return; \
        _PRTOS_HCALL1(_a0, _hc##_nr, _r);        \
    }

#define prtos_hcall1r(_hc, _t0, _a0)                       \
    __stdcall prtos_s32_t prtos_##_hc(_t0 _a0) {           \
        prtos_s32_t _r;                                    \
        if ((_r = prtos_flush_hyp_batch()) < 0) return _r; \
        _PRTOS_HCALL1(_a0, _hc##_nr, _r);                  \
        return _r;                                         \
    }

#define prtos_hcall2(_hc, _t0, _a0, _t1, _a1)      \
    __stdcall void prtos_##_hc(_t0 _a0, _t1 _a1) { \
        prtos_s32_t _r;                            \
        if (prtos_flush_hyp_batch() < 0) return;   \
        _PRTOS_HCALL2(_a0, _a1, _hc##_nr, _r);     \
    }

#define prtos_hcall2r(_hc, _t0, _a0, _t1, _a1)             \
    __stdcall prtos_s32_t prtos_##_hc(_t0 _a0, _t1 _a1) {  \
        prtos_s32_t _r;                                    \
        if ((_r = prtos_flush_hyp_batch()) < 0) return _r; \
        _PRTOS_HCALL2(_a0, _a1, _hc##_nr, _r);             \
        return _r;                                         \
    }

#define prtos_hcall3(_hc, _t0, _a0, _t1, _a1, _t2, _a2)     \
    __stdcall void prtos_##_hc(_t0 _a0, _t1 _a1, _t2 _a2) { \
        prtos_s32_t _r;                                     \
        if (prtos_flush_hyp_batch() < 0) return;            \
        _PRTOS_HCALL3(_a0, _a1, _a2, _hc##_nr, _r);         \
    }

#define prtos_hcall3r(_hc, _t0, _a0, _t1, _a1, _t2, _a2)           \
    __stdcall prtos_s32_t prtos_##_hc(_t0 _a0, _t1 _a1, _t2 _a2) { \
        prtos_s32_t _r;                                            \
        if ((_r = prtos_flush_hyp_batch()) < 0) return _r;         \
        _PRTOS_HCALL3(_a0, _a1, _a2, _hc##_nr, _r);                \
        return _r;                                                 \
    }

#define prtos_hcall4(_hc, _t0, _a0, _t1, _a1, _t2, _a2, _t3, _a3)    \
    __stdcall void prtos_##_hc(_t0 _a0, _t1 _a1, _t2 _a2, _t3 _a3) { \
        prtos_s32_t _r;                                              \
        if (prtos_flush_hyp_batch() < 0) return;                     \
        _PRTOS_HCALL4(_a0, _a1, _a2, _a3, _hc##_nr, _r);             \
    }

#define prtos_hcall4r(_hc, _t0, _a0, _t1, _a1, _t2, _a2, _t3, _a3)          \
    __stdcall prtos_s32_t prtos_##_hc(_t0 _a0, _t1 _a1, _t2 _a2, _t3 _a3) { \
        prtos_s32_t _r;                                                     \
        if ((_r = prtos_flush_hyp_batch()) < 0) return _r;                  \
        _PRTOS_HCALL4(_a0, _a1, _a2, _a3, _hc##_nr, _r);                    \
        return _r;                                                          \
    }

#define prtos_hcall5(_hc, _t0, _a0, _t1, _a1, _t2, _a2, _t3, _a3, _t4, _a4)   \
    __stdcall void prtos_##_hc(_t0 _a0, _t1 _a1, _t2 _a2, _t3 _a3, _t4 _a4) { \
        prtos_s32_t _r;                                                       \
        if (prtos_flush_hyp_batch() < 0) return;                              \
        _PRTOS_HCALL5(_a0, _a1, _a2, _a3, _a4, _hc##_nr, _r);                 \
    }

#define prtos_hcall5r(_hc, _t0, _a0, _t1, _a1, _t2, _a2, _t3, _a3, _t4, _a4)         \
    __stdcall prtos_s32_t prtos_##_hc(_t0 _a0, _t1 _a1, _t2 _a2, _t3 _a3, _t4 _a4) { \
        prtos_s32_t _r;                                                              \
        if ((_r = prtos_flush_hyp_batch()) < 0) return _r;                           \
        _PRTOS_HCALL5(_a0, _a1, _a2, _a3, _a4, _hc##_nr, _r);                        \
        return _r;                                                                   \
    }

typedef struct {
    prtos_u32_t high;
    prtos_u32_t low;
} generic_desc_t;

#else

// Parameters ->
// a0: syscall number
// a1: 1st parameter
// a2: 2nd parameter
// ...

#define __PRTOS_HC ecall

#endif
#endif
