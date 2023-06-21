/*
 * FILE: xef.h
 *
 * prtos's executable format helper functions
 *
 * www.prtos.org
 */

#ifndef _LIB_PRTOS_XEF_H_
#define _LIB_PRTOS_XEF_H_

#include <prtos_inc/prtosef.h>

#define XEF_OK 0
#define XEF_BAD_SIGNATURE -1
#define XEF_INCORRECT_VERSION -2
#define XEF_UNMATCHING_DIGEST -3

struct xef_file {
    struct xef_hdr *hdr;
    struct xef_segment *segment_table;
#if 0
#ifdef CONFIG_IA32
    struct xefRel *relTab;
    struct xefRela *relaTab;
#endif
#endif
    struct xef_custom_file *custom_file_table;
    prtos_u8_t *image;
};

extern prtos_s32_t parse_xef_file(prtos_u8_t *img, struct xef_file *xef_file);

extern void *load_xef_file(struct xef_file *xef_file, int (*vaddr_to_paddr)(void *, prtos_s32_t, prtos_address_t, prtos_address_t *), void *memory_areas,
                           prtos_s32_t num_of_areas);
extern void *load_xef_custom_file(struct xef_file *xef_custom_file, struct xef_custom_file *customFile);

#endif
