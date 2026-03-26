/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * version.h
 *
 * PRTOS version, type, and compile information.
 *
 * Copyright (c) 2005, Nguyen Anh Quynh <aquynh@gmail.com>
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __PRTOS_PUBLIC_VERSION_H__
#define __PRTOS_PUBLIC_VERSION_H__

#include "public_prtos.h"

/* NB. All ops return zero on success, except PRTOSVER_{version,pagesize}
 * PRTOSVER_{version,pagesize,build_id} */

/* arg == NULL; returns major:minor (16:16). */
#define PRTOSVER_version      0

/* arg == prtos_extraversion_t. */
#define PRTOSVER_extraversion 1
typedef char prtos_extraversion_t[16];
#define PRTOS_EXTRAVERSION_LEN (sizeof(prtos_extraversion_t))

/* arg == prtos_compile_info_t. */
#define PRTOSVER_compile_info 2
struct prtos_compile_info {
    char compiler[64];
    char compile_by[16];
    char compile_domain[32];
    char compile_date[32];
};
typedef struct prtos_compile_info prtos_compile_info_t;

#define PRTOSVER_capabilities 3
typedef char prtos_capabilities_info_t[1024];
#define XEN_CAPABILITIES_INFO_LEN (sizeof(prtos_capabilities_info_t))

#define PRTOSVER_changeset 4
typedef char prtos_changeset_info_t[64];
#define PRTOS_CHANGESET_INFO_LEN (sizeof(prtos_changeset_info_t))

/*
 * This API is problematic.
 *
 * It is only applicable to guests which share pagetables with PRTOS (x86 PV
 * guests), but unfortunately has leaked into other guest types and
 * architectures with an expectation of never failing.
 *
 * It is intended to identify the virtual address split between guest kernel
 * and PRTOS.
 *
 * For 32bit PV guests, there is a split, and it is variable (between two
 * fixed bounds), and this boundary is reported to guests.  The detail missing
 * from the hypercall is that the second boundary is the 32bit architectural
 * boundary at 4G.
 *
 * For 64bit PV guests, PRTOS lives at the bottom of the upper canonical range.
 * This hypercall happens to report the architectural boundary, not the one
 * which would be necessary to make a variable split work.  As such, this
 * hypercall entirely useless for 64bit PV guests, and all inspected
 * implementations at the time of writing were found to have compile time
 * expectations about the split.
 *
 * For architectures where this hypercall is implemented, for backwards
 * compatibility with the expectation of the hypercall never failing PRTOS will
 * return 0 instead of failing with -ENOSYS in cases where the guest should
 * not be making the hypercall.
 */
#define PRTOSVER_platform_parameters 5
struct prtos_platform_parameters {
    prtos_ulong_t virt_start;
};
typedef struct prtos_platform_parameters prtos_platform_parameters_t;

#define PRTOSVER_get_features 6
struct xen_feature_info {
    uint32_t     submap_idx;    /* IN: which 32-bit submap to return */
    uint32_t     submap;        /* OUT: 32-bit submap */
};
typedef struct xen_feature_info prtos_feature_info_t;

/* Declares the features reported by PRTOSVER_get_features. */
#include "public_features.h"

/* arg == NULL; returns host memory page size. */
#define PRTOSVER_pagesize 7

/* arg == prtos_domain_handle_t.
 *
 * The toolstack fills it out for guest consumption. It is intended to hold
 * the UUID of the guest.
 */
#define PRTOSVER_guest_handle 8

#define PRTOSVER_commandline 9
typedef char prtos_commandline_t[1024];

/*
 * Return value is the number of bytes written, or XEN_Exx on error.
 * Calling with empty parameter returns the size of build_id.
 */
#define PRTOSVER_build_id 10
struct prtos_build_id {
        uint32_t        len; /* IN: size of buf[]. */
        unsigned char   buf[PRTOS_FLEX_ARRAY_DIM];
                             /* OUT: Variable length buffer with build_id. */
};
typedef struct prtos_build_id prtos_build_id_t;

#endif /* __PRTOS_PUBLIC_VERSION_H__ */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
