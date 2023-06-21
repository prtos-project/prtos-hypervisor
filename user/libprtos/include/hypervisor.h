/*
 * FILE: hypervisor.h
 *
 * hypervisor management functions
 *
 * www.prtos.org
 */

#ifndef _LIB_PRTOS_HYPERVISOR_H_
#define _LIB_PRTOS_HYPERVISOR_H_

#include <prtos_inc/config.h>
#include <prtos_inc/prtosconf.h>
#include <arch/hypervisor.h>

#include <prtos_inc/linkage.h>

#define PRTOS_PARTITION_SELF (PCT_GET_PARTITION_ID(prtos_get_pct0()))
#define PRTOS_VCPU_SELF (PCT_GET_VCPU_ID(prtos_get_pct()))

/* Partition management */
extern __stdcall prtos_s32_t prtos_write_console(char *buffer, prtos_s32_t length);
extern __stdcall prtos_s32_t prtos_memory_copy(prtos_id_t dest_id, prtos_u32_t dest_addr, prtos_id_t src_id, prtos_u32_t src_addr, prtos_u32_t size);

/* Deferred hypercalls */
extern void init_batch(void);
extern __stdcall prtos_s32_t prtos_flush_hyp_batch(void);
extern __stdcall void prtos_lazy_hypercall(prtos_u32_t hypercall_nr, prtos_s32_t num_of_args, ...);

#define prtos_lazy_hcall0(_hc)              \
    __stdcall void prtos_lazy_##_hc(void) { \
        prtos_lazy_hypercall(_hc##_nr, 0);  \
    }

#define prtos_lazy_hcall1(_hc, _t0, _a0)        \
    __stdcall void prtos_lazy_##_hc(_t0 _a0) {  \
        prtos_lazy_hypercall(_hc##_nr, 1, _a0); \
    }

#define prtos_lazy_hcall2(_hc, _t0, _a0, _t1, _a1)      \
    __stdcall void prtos_lazy_##_hc(_t0 _a0, _t1 _a1) { \
        prtos_lazy_hypercall(_hc##_nr, 2, _a0, _a1);    \
    }

#define prtos_lazy_hcall3(_hc, _t0, _a0, _t1, _a1, _t2, _a2)     \
    __stdcall void prtos_lazy_##_hc(_t0 _a0, _t1 _a1, _t2 _a2) { \
        prtos_lazy_hypercall(_hc##_nr, 3, _a0, _a1, _a2);        \
    }

#define prtos_lazy_hcall4(_hc, _t0, _a0, _t1, _a1, _t2, _a2, _t3, _a3)    \
    __stdcall void prtos_lazy_##_hc(_t0 _a0, _t1 _a1, _t2 _a2, _t3 _a3) { \
        prtos_lazy_hypercall(_hc##_nr, 4, _a0, _a1, _a2, _a3);            \
    }

#define prtos_lazy_hcall5(_hc, _t0, _a0, _t1, _a1, _t2, _a2, _t3, _a3, _t4, _a4)   \
    __stdcall void prtos_lazy_##_hc(_t0 _a0, _t1 _a1, _t2 _a2, _t3 _a3, _t4 _a4) { \
        prtos_lazy_hypercall(_hc##_nr, 5, _a0, _a1, _a2, _a3, _a4);                \
    }

#endif
