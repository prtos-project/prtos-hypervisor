/*
 * FILE: container.h
 *
 * Container definition
 *
 * www.prtos.org
 */

#ifndef _LIB_PRTOS_CONTAINER_H_
#define _LIB_PRTOS_CONTAINER_H_

#include <prtos_inc/config.h>

struct xef_container_file {
    struct prtos_exec_container_hdr *hdr;
    struct prtos_exec_file *file_table;
    struct prtos_exec_partition *part_table;
    prtos_u8_t *image;
};

#define CONTAINER_OK 0
#define CONTAINER_BAD_SIGNATURE -1
#define CONTAINER_INCORRECT_VERSION -2
#define CONTAINER_UNMATCHING_DIGEST -3

extern prtos_s32_t parse_xef_container(prtos_u8_t *img, struct xef_container_file *pack);

#endif
