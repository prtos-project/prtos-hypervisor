/*
 * FILE: container.c
 *
 * Container definition
 *
 * www.prtos.org
 */

#include <container.h>
#include <prtos_inc/prtosef.h>
#include <prtos_inc/digest.h>

#define OFFSETOF(_type, _member) ((prtos_u_size_t)PTR2ADDR(&((_type *)0)->_member))

prtos_s32_t parse_xef_container(prtos_u8_t *img, struct xef_container_file *c) {
    prtos_u8_t digest[PRTOS_DIGEST_BYTES];
    struct digest_ctx digestState;
    prtos_s32_t e;

    c->hdr = (struct prtos_exec_container_hdr *)img;
    if (c->hdr->signature != PRTOS_PACKAGE_SIGNATURE) return CONTAINER_BAD_SIGNATURE;

    if ((PRTOS_GET_VERSION(c->hdr->version) != PRTOSPACK_VERSION) && (PRTOS_GET_SUBVERSION(c->hdr->version) != PRTOSPACK_SUBVERSION))
        return CONTAINER_INCORRECT_VERSION;
    c->file_table = (struct prtos_exec_file *)(img + c->hdr->file_table_offset);
    c->part_table = (struct prtos_exec_partition *)(img + c->hdr->part_table_offset);
    c->image = img;

    if (c->hdr->flags & PRTOSEF_CONTAINER_DIGEST) {
        for (e = 0; e < PRTOS_DIGEST_BYTES; e++) digest[e] = 0;
        digest_init(&digestState);
        digest_update(&digestState, img, OFFSETOF(struct prtos_exec_container_hdr, digest));
        digest_update(&digestState, digest, PRTOS_DIGEST_BYTES);
        digest_update(&digestState, &img[OFFSETOF(struct prtos_exec_container_hdr, file_size)],
                      c->hdr->file_size - OFFSETOF(struct prtos_exec_container_hdr, file_size));
        digest_final(digest, &digestState);
        for (e = 0; e < PRTOS_DIGEST_BYTES; e++)
            if (digest[e] != c->hdr->digest[e]) return CONTAINER_UNMATCHING_DIGEST;
    }

    return CONTAINER_OK;
}
