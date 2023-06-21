/*
 * FILE: prtoshypercalls.h
 *
 * Arch hypercalls definition
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
#include <prtos_inc/arch/segments.h>

#include <prtos_inc/linkage.h>

#ifndef __ASSEMBLY__

#define __prtos_cli() prtos_x86_clear_if()
#define __prtos_sti() prtos_x86_set_if()

#define __DO_PRTOSHC "lcall $(" TO_STR(PRTOS_HYPERCALL_CALLGATE_SEL) "), $0x0"

#define _PRTOS_HCALL0(_hc_nr, _r) __asm__ __volatile__(__DO_PRTOSHC : "=a"(_r) : "0"(_hc_nr))

#define _PRTOS_HCALL1(_a0, _hc_nr, _r) __asm__ __volatile__(__DO_PRTOSHC : "=a"(_r) : "0"(_hc_nr), "b"((prtos_u32_t)(_a0)))

#define _PRTOS_HCALL2(_a0, _a1, _hc_nr, _r) __asm__ __volatile__(__DO_PRTOSHC : "=a"(_r) : "0"(_hc_nr), "b"((prtos_u32_t)(_a0)), "c"((prtos_u32_t)(_a1)))

#define _PRTOS_HCALL3(_a0, _a1, _a2, _hc_nr, _r) \
    __asm__ __volatile__(__DO_PRTOSHC : "=a"(_r) : "0"(_hc_nr), "b"((prtos_u32_t)(_a0)), "c"((prtos_u32_t)(_a1)), "d"((prtos_u32_t)(_a2)))

#define _PRTOS_HCALL4(_a0, _a1, _a2, _a3, _hc_nr, _r) \
    __asm__ __volatile__(__DO_PRTOSHC                 \
                         : "=a"(_r)                   \
                         : "0"(_hc_nr), "b"((prtos_u32_t)(_a0)), "c"((prtos_u32_t)(_a1)), "d"((prtos_u32_t)(_a2)), "S"((prtos_u32_t)(_a3)))

#define _PRTOS_HCALL5(_a0, _a1, _a2, _a3, _a4, _hc_nr, _r)                                                                                  \
    __asm__ __volatile__(__DO_PRTOSHC                                                                                                       \
                         : "=a"(_r)                                                                                                         \
                         : "0"(_hc_nr), "b"((prtos_u32_t)(_a0)), "c"((prtos_u32_t)(_a1)), "d"((prtos_u32_t)(_a2)), "S"((prtos_u32_t)(_a3)), \
                           "D"((prtos_u32_t)(_a4)))

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
} genericDesc_t;

extern __stdcall prtos_s32_t prtos_x86_load_cr0(prtos_word_t val);
extern __stdcall prtos_s32_t prtos_x86_load_cr3(prtos_word_t val);
extern __stdcall prtos_s32_t prtos_x86_load_cr4(prtos_word_t val);
extern __stdcall prtos_s32_t prtos_x86_load_tss(struct x86_tss *t);
extern __stdcall prtos_s32_t prtos_x86_load_gdt(struct x86_desc_reg *desc);
extern __stdcall prtos_s32_t prtos_x86_load_idtr(struct x86_desc_reg *desc);
extern __stdcall prtos_s32_t prtos_x86_update_ss_sp(prtos_word_t ss, prtos_word_t sp, prtos_u32_t level);
extern __stdcall prtos_s32_t prtos_x86_update_gdt(prtos_s32_t entry, struct x86_desc *gdt);
extern __stdcall prtos_s32_t prtos_x86_update_idt(prtos_s32_t entry, struct x86_gate *desc);
extern __stdcall void prtos_x86_set_if(void);
extern __stdcall void prtos_x86_clear_if(void);
extern __stdcall prtos_s32_t prtos_x86_set_watchpoint(prtos_address_t v_addr);
extern void prtos_x86_iret(void);

extern __stdcall void prtos_lazy_x86_load_cr0(prtos_word_t val);
extern __stdcall void prtos_lazy_x86_load_cr3(prtos_word_t val);
extern __stdcall void prtos_lazy_x86_load_cr4(prtos_word_t val);
extern __stdcall void prtos_lazy_x86_load_tss(struct x86_tss *t);
extern __stdcall void prtos_lazy_x86_load_gdt(struct x86_desc_reg *desc);
extern __stdcall void prtos_lazy_x86_load_idtr(struct x86_desc_reg *desc);
extern __stdcall void prtos_lazy_x86_update_ss_sp(prtos_word_t ss, prtos_word_t sp, prtos_u32_t level);
extern __stdcall void prtos_lazy_x86_update_gdt(prtos_s32_t entry, struct x86_desc *gdt);
extern __stdcall void prtos_lazy_x86_update_idt(prtos_s32_t entry, struct x86_gate *desc);
#else

// Parameters ->
// eax: syscall number
// ebx: 1st parameter
// ecx: 2nd parameter
// ...

#define __PRTOS_HC lcall $(PRTOS_HYPERCALL_CALLGATE_SEL), $0x0

#endif
#endif
