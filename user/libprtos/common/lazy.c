/*
 * FILE: lazy.c
 *
 * Deferred hypercalls
 *
 * www.prtos.org
 */

#include <prtos.h>

typedef __builtin_va_list va_list;

#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, l) __builtin_va_arg(v, l)

#define PRTOS_BATCH_LEN 1024

static volatile prtos_u32_t prtos_hypercall_batch[PRTOS_BATCH_LEN];
static volatile prtos_s32_t batch_len = 0, prev_batch_len = 0;

__stdcall prtos_s32_t prtos_flush_hyp_batch(void) {
    if (batch_len) {
        prtos_u32_t r;
        if ((r = prtos_multicall((void *)prtos_hypercall_batch, (void *)&prtos_hypercall_batch[batch_len])) < 0) return r;
        prev_batch_len = batch_len;
        batch_len = 0;
    }
    return PRTOS_OK;
}

__stdcall void prtos_lazy_hypercall(prtos_u32_t hypercall_nr, prtos_s32_t num_of_args, ...) {
    va_list args;
    prtos_s32_t e;
    if ((batch_len >= PRTOS_BATCH_LEN) || ((batch_len + num_of_args + 1) >= PRTOS_BATCH_LEN)) prtos_flush_hyp_batch();
    prtos_hypercall_batch[batch_len++] = hypercall_nr;
    prtos_hypercall_batch[batch_len++] = num_of_args;
    va_start(args, num_of_args);
    for (e = 0; e < num_of_args; e++) prtos_hypercall_batch[batch_len++] = va_arg(args, prtos_u32_t);
    va_end(args);
}

void init_batch(void) {
    batch_len = 0;
    prev_batch_len = 0;
}
