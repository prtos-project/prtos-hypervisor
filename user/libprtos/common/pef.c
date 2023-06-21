/*
 * FILE: pef.c
 *
 * www.prtos.org
 */

#include <pef.h>
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

void init_pef_parser() {}

prtos_s32_t parse_pef_file(prtos_u8_t *img, struct pef_file *pef_file) {
    prtos_u8_t digest[PRTOS_DIGEST_BYTES];
    struct digest_ctx digest_state;
    prtos_s32_t e;

    pef_file->hdr = (struct pef_hdr *)img;
    if (RWORD(pef_file->hdr->signature) != PEF_SIGNATURE) return PEF_BAD_SIGNATURE;

    if ((PRTOS_GET_VERSION(RWORD(pef_file->hdr->version)) != PEF_VERSION) && (PRTOS_GET_SUBVERSION(RWORD(pef_file->hdr->version)) != PEF_SUBVERSION))
        return PEF_INCORRECT_VERSION;

    pef_file->segment_table = (struct pef_segment *)(img + RWORD(pef_file->hdr->segment_table_offset));
    pef_file->custom_file_table = (struct pef_custom_file *)(img + RWORD(pef_file->hdr->custom_file_table_offset));
    pef_file->image = img + RWORD(pef_file->hdr->image_offset);
    if (RWORD(pef_file->hdr->flags) & PEF_DIGEST) {
        for (e = 0; e < PRTOS_DIGEST_BYTES; e++) digest[e] = 0;
        digest_init(&digest_state);
        digest_update(&digest_state, img, OFFSETOF(struct pef_hdr, digest));
        digest_update(&digest_state, (prtos_u8_t *)digest, PRTOS_DIGEST_BYTES);
        digest_update(&digest_state, &img[(prtos_u32_t)OFFSETOF(struct pef_hdr, payload)],
                      RWORD(pef_file->hdr->file_size) - (prtos_u32_t)OFFSETOF(struct pef_hdr, payload));
        digest_final(digest, &digest_state);

        for (e = 0; e < PRTOS_DIGEST_BYTES; e++) {
            if (digest[e] != pef_file->hdr->digest[e]) return PEF_UNMATCHING_DIGEST;
        }
    }

    return PEF_OK;
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

void *load_pef_file(struct pef_file *pef_file, int (*vaddr_to_paddr)(void *, prtos_s32_t, prtos_address_t, prtos_address_t *), void *memory_areas,
                    prtos_s32_t num_of_areas) {
    prtos_s32_t e;
    prtos_address_t addr;
    if (pef_file->hdr->num_of_segments <= 0) return 0;

    for (e = 0; e < pef_file->hdr->num_of_segments; e++) {
        if (pef_file->hdr->flags & PEF_COMPRESSED) {
            prtos_u8_t *read_ptr, *write_ptr;
            addr = pef_file->segment_table[e].phys_addr;
            if (vaddr_to_paddr) vaddr_to_paddr(memory_areas, num_of_areas, addr, &addr);
            read_ptr = (prtos_u8_t *)&pef_file->image[pef_file->segment_table[e].offset];
            write_ptr = (prtos_u8_t *)ADDR2PTR(addr);  // pef_file->segment_table[e].phys_addr);
            if (uncompress(pef_file->segment_table[e].deflated_file_size, pef_file->segment_table[e].file_size, u_read, &read_ptr, u_write, &write_ptr) < 0)
                return 0;
        } else {
            addr = pef_file->segment_table[e].phys_addr;
            if (vaddr_to_paddr) vaddr_to_paddr(memory_areas, num_of_areas, addr, &addr);
            memcpy(ADDR2PTR(addr),  // pef_file->segment_table[e].phys_addr),
                   &pef_file->image[pef_file->segment_table[e].offset], pef_file->segment_table[e].file_size);
        }
    }

    addr = pef_file->hdr->prtos_image_hdr;
    if (vaddr_to_paddr) vaddr_to_paddr(memory_areas, num_of_areas, addr, &addr);
    return ADDR2PTR(addr);
}

void *load_pef_custom_file(struct pef_file *pef_custom_file, struct pef_custom_file *custom_file) {
    prtos_s32_t e;

    if (pef_custom_file->hdr->num_of_segments <= 0) return 0;

    for (e = 0; e < pef_custom_file->hdr->num_of_segments; e++) {
        if (pef_custom_file->hdr->flags & PEF_COMPRESSED) {
            prtos_u8_t *read_ptr, *write_ptr;
            read_ptr = (prtos_u8_t *)&pef_custom_file->image[pef_custom_file->segment_table[e].offset];
            write_ptr = (prtos_u8_t *)ADDR2PTR(custom_file->start_addr);
            if (uncompress(pef_custom_file->segment_table[e].deflated_file_size, pef_custom_file->segment_table[e].file_size, u_read, &read_ptr, u_write,
                           &write_ptr) < 0)
                return 0;
        } else {
            memcpy(ADDR2PTR(custom_file->start_addr), &pef_custom_file->image[pef_custom_file->segment_table[e].offset],
                   pef_custom_file->segment_table[e].file_size);
        }
    }

    return ADDR2PTR(custom_file->start_addr);
}
#endif
