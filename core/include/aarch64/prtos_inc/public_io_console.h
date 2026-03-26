/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * console.h
 *
 * Console I/O interface for PRTOS guest OSes.
 *
 * Copyright (c) 2005, Keir Fraser
 */

#ifndef __PRTOS_PUBLIC_IO_CONSOLE_H__
#define __PRTOS_PUBLIC_IO_CONSOLE_H__

typedef uint32_t PRTOSCONS_RING_IDX;

#define MASK_PRTOSCONS_IDX(idx, ring) ((idx) & (sizeof(ring)-1))

struct prtoscons_interface {
    char in[1024];
    char out[2048];
    PRTOSCONS_RING_IDX in_cons, in_prod;
    PRTOSCONS_RING_IDX out_cons, out_prod;
};

#ifdef PRTOS_WANT_FLEX_CONSOLE_RING
#include "public_io_ring.h"
DEFINE_PRTOS_FLEX_RING(prtoscons);
#endif

#endif /* __PRTOS_PUBLIC_IO_CONSOLE_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
