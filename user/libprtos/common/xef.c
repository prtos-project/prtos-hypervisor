/*
 * FILE: xef.c
 *
 * www.prtos.org
 */

#include <xef.h>
#include <prtos_inc/digest.h>
#include <prtos_inc/compress.h>
#include <prtos_inc/prtosef.h>

#include <endianess.h>

#undef OFFSETOF
#ifdef __compiler_offsetof
#define OFFSETOF(_type, _member) __compiler_offsetof(_type, _member)
#else
#ifdef HOST
#define OFFSETOF(_type, _member) (unsigned long)(&((_type *)0)->_member)
#else
#define OFFSETOF(_type, _member) PTR2ADDR(&((_type *)0)->_member)
#endif
#endif

extern void *memcpy(void *dest, const void *src, unsigned long n);

void init_xef_parser() {}

prtos_s32_t parse_xef_file(prtos_u8_t *img, struct xef_file *xef_file) {
    prtos_u8_t digest[PRTOS_DIGEST_BYTES];
    struct digest_ctx digestState;
    prtos_s32_t e;

    xef_file->hdr = (struct xef_hdr *)img;
    if (RWORD(xef_file->hdr->signature) != XEF_SIGNATURE) return XEF_BAD_SIGNATURE;

    if ((PRTOS_GET_VERSION(RWORD(xef_file->hdr->version)) != XEF_VERSION) && (PRTOS_GET_SUBVERSION(RWORD(xef_file->hdr->version)) != XEF_SUBVERSION))
        return XEF_INCORRECT_VERSION;

    xef_file->segment_table = (struct xef_segment *)(img + RWORD(xef_file->hdr->segment_table_offset));
    xef_file->custom_file_table = (struct xef_custom_file *)(img + RWORD(xef_file->hdr->custom_file_table_offset));
#ifdef CONFIG_IA32
    xef_file->relTab = (struct xefRel *)(img + RWORD(xef_file->hdr->relOffset));
    xef_file->relaTab = (struct xefRela *)(img + RWORD(xef_file->hdr->relaOffset));
#endif
    xef_file->image = img + RWORD(xef_file->hdr->image_offset);
    if (RWORD(xef_file->hdr->flags) & XEF_DIGEST) {
        for (e = 0; e < PRTOS_DIGEST_BYTES; e++) digest[e] = 0;
        digest_init(&digestState);
        digest_update(&digestState, img, OFFSETOF(struct xef_hdr, digest));
        digest_update(&digestState, (prtos_u8_t *)digest, PRTOS_DIGEST_BYTES);
        digest_update(&digestState, &img[(prtos_u32_t)OFFSETOF(struct xef_hdr, payload)],
                      RWORD(xef_file->hdr->file_size) - (prtos_u32_t)OFFSETOF(struct xef_hdr, payload));
        digest_final(digest, &digestState);

        for (e = 0; e < PRTOS_DIGEST_BYTES; e++) {
            if (digest[e] != xef_file->hdr->digest[e]) return XEF_UNMATCHING_DIGEST;
        }
    }

    return XEF_OK;
}

#ifndef HOST
static prtos_s32_t u_read(void *b, prtos_u_size_t s, void *d) {
    memcpy(b, *(prtos_u8_t **)d, s);
    *(prtos_u8_t **)d += s;
    return s;
}

static prtos_s32_t u_write(void *b, prtos_u_size_t s, void *d) {
    memcpy(*(prtos_u8_t **)d, b, s);
    *(prtos_u8_t **)d += s;
    return s;
}

void *load_xef_file(struct xef_file *xef_file, int (*vaddr_to_paddr)(void *, prtos_s32_t, prtos_address_t, prtos_address_t *), void *memory_areas,
                    prtos_s32_t num_of_areas) {
    prtos_s32_t e;
    prtos_address_t addr;
    if (xef_file->hdr->num_of_segments <= 0) return 0;

    for (e = 0; e < xef_file->hdr->num_of_segments; e++) {
        if (xef_file->hdr->flags & XEF_COMPRESSED) {
            prtos_u8_t *read_ptr, *write_ptr;
            addr = xef_file->segment_table[e].phys_addr;
            if (vaddr_to_paddr) vaddr_to_paddr(memory_areas, num_of_areas, addr, &addr);
            read_ptr = (prtos_u8_t *)&xef_file->image[xef_file->segment_table[e].offset];
            write_ptr = (prtos_u8_t *)ADDR2PTR(addr);  // xef_file->segment_table[e].phys_addr);
            if (uncompress(xef_file->segment_table[e].deflated_file_size, xef_file->segment_table[e].file_size, u_read, &read_ptr, u_write, &write_ptr) < 0)
                return 0;
        } else {
            addr = xef_file->segment_table[e].phys_addr;
            if (vaddr_to_paddr) vaddr_to_paddr(memory_areas, num_of_areas, addr, &addr);
            memcpy(ADDR2PTR(addr),  // xef_file->segment_table[e].phys_addr),
                   &xef_file->image[xef_file->segment_table[e].offset], xef_file->segment_table[e].file_size);
        }
    }

    addr = xef_file->hdr->prtos_image_hdr;
    if (vaddr_to_paddr) vaddr_to_paddr(memory_areas, num_of_areas, addr, &addr);
    return ADDR2PTR(addr);
}

void *load_xef_custom_file(struct xef_file *xef_custom_file, struct xef_custom_file *custom_file) {
    prtos_s32_t e;

    if (xef_custom_file->hdr->num_of_segments <= 0) return 0;

    for (e = 0; e < xef_custom_file->hdr->num_of_segments; e++) {
        if (xef_custom_file->hdr->flags & XEF_COMPRESSED) {
            prtos_u8_t *read_ptr, *write_ptr;
            read_ptr = (prtos_u8_t *)&xef_custom_file->image[xef_custom_file->segment_table[e].offset];
            write_ptr = (prtos_u8_t *)ADDR2PTR(custom_file->sAddr);
            if (uncompress(xef_custom_file->segment_table[e].deflated_file_size, xef_custom_file->segment_table[e].file_size, u_read, &read_ptr, u_write,
                           &write_ptr) < 0)
                return 0;
        } else {
            memcpy(ADDR2PTR(custom_file->sAddr), &xef_custom_file->image[xef_custom_file->segment_table[e].offset],
                   xef_custom_file->segment_table[e].file_size);
        }
    }

    return ADDR2PTR(custom_file->sAddr);
}
#endif
