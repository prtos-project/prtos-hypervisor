/*
 * FILE: pef.h
 *
 * prtos's executable format helper functions
 *
 * www.prtos.org
 */

#ifndef _LIB_PRTOS_PEF_H_
#define _LIB_PRTOS_PEF_H_

#include <prtos_inc/prtosef.h>

#define PEF_OK 0
#define PEF_BAD_SIGNATURE -1
#define PEF_INCORRECT_VERSION -2
#define PEF_UNMATCHING_DIGEST -3

struct pef_file {
    struct pef_hdr *hdr;
    struct pef_segment *segment_table;
    struct pef_custom_file *custom_file_table;
    prtos_u8_t *image;
};

extern prtos_s32_t parse_pef_file(prtos_u8_t *img, struct pef_file *pef_file);

extern void *load_pef_file(struct pef_file *pef_file, int (*vaddr_to_paddr)(void *, prtos_s32_t, prtos_address_t, prtos_address_t *), void *memory_areas,
                           prtos_s32_t num_of_areas);
extern void *load_pef_custom_file(struct pef_file *pef_custom_file, struct pef_custom_file *custom_file);

#endif
