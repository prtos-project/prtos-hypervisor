/*
 * FILE: main.c
 *
 * www.prtos.org
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <elf.h>
#include <stddef.h>
#include <xeftool.h>

#include <prtos_inc/digest.h>
#include <prtos_inc/compress.h>
#include <prtos_inc/arch/arch_types.h>
#include <prtos_inc/prtosef.h>

#define TOOL_NAME "prtoseformat"
#define USAGE                               \
    "Usage: " TOOL_NAME " [read] [build]\n" \
    " \tread [-h|-s|-m] <input>\n"          \
    " \tbuild [-m] [-o <output>] [-c] [-p <payload_file>] <input>\n"

struct xef_hdr xef_hdr, *xef_hdr_read;
struct xef_segment *xef_segment_table;
struct xefRela *xef_rela_table;
struct xefRel *xef_rel_table;
struct xef_custom_file *xef_custom_file_table;
prtos_u8_t *image;

void error_printf(char *fmt, ...) {
    va_list args;

    fflush(stdout);
    if (TOOL_NAME != NULL) fprintf(stderr, "%s: ", TOOL_NAME);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    if (fmt[0] != '\0' && fmt[strlen(fmt) - 1] == ':') fprintf(stderr, " %s", strerror(errno));
    fprintf(stderr, "\n");
    exit(2); /* conventional value for failed execution */
}

#if defined(CONFIG_x86)
#define EM_ARCH EM_386
#define ELF(x) Elf32_##x
#define PRINT_PREF
#endif

#ifdef FORCE_ELF32
#undef ELF
#define ELF(x) Elf32_##x
#undef EM_ARCH
#define EM_ARCH EM_386
#endif

static int parse_elf_image(int fd_elf) {
    struct prtos_image_hdr *prtos_image_hdr = 0;
    struct xef_segment *xef_sect;
    struct prtos_hdr *prtos_hdr = 0;
    struct xef_custom_file *custom_file_table;
    ELF(Ehdr) e_hdr;
    ELF(Phdr) * pHdr;
    ELF(Shdr) * sHdr;
    char *elf_str_table = 0;
    int e;

    custom_file_table = 0;
    xef_hdr.num_of_segments = 0;
    xef_segment_table = 0;
    xef_custom_file_table = 0;
    xef_rel_table = 0;
    xef_rela_table = 0;
    image = 0;
    xef_hdr.image_length = 0;

    lseek(fd_elf, 0, SEEK_SET);
    DO_READ(fd_elf, &e_hdr, sizeof(ELF(Ehdr)));
    if ((RHALF(e_hdr.e_type) != ET_EXEC) || (e_hdr.e_machine != RHALF(EM_ARCH)) || (e_hdr.e_phentsize != RHALF(sizeof(ELF(Phdr)))))
        error_printf("Malformed ELF header");

    // Looking for .prtos_hdr/.prtos_image_hdr sections
    sHdr = malloc(sizeof(ELF(Shdr)) * RHALF(e_hdr.e_shnum));
    lseek(fd_elf, RWORD(e_hdr.e_shoff), SEEK_SET);
    DO_READ(fd_elf, sHdr, sizeof(ELF(Shdr)) * RHALF(e_hdr.e_shnum));

// Locating the string table
#if (__GNUC__ >= 4 && __GNUC__ < 5)
    for (e = 0; e < RHALF(e_hdr.e_shnum); e++)
        if (RWORD(sHdr[e].sh_type) == SHT_STRTAB) {
            DO_MALLOC(elf_str_table, RWORD(sHdr[e].sh_size));
            lseek(fd_elf, RWORD(sHdr[e].sh_offset), SEEK_SET);
            DO_READ(fd_elf, elf_str_table, RWORD(sHdr[e].sh_size));
            break;
        }
#else
    Elf32_Shdr SHSTRTAB;
    lseek(fd_elf, 0, SEEK_SET);
    lseek(fd_elf, e_hdr.e_shoff + (e_hdr.e_shentsize * e_hdr.e_shstrndx), SEEK_SET);
    DO_READ(fd_elf, &SHSTRTAB, e_hdr.e_shentsize);
    lseek(fd_elf, SHSTRTAB.sh_offset, SEEK_SET);
    DO_MALLOC(elf_str_table, RWORD(SHSTRTAB.sh_size));
    DO_READ(fd_elf, elf_str_table, RWORD(SHSTRTAB.sh_size));
#endif

    if (!elf_str_table) error_printf("ELF string table not found");

    xef_hdr.entry_point = e_hdr.e_entry;
    for (e = 0; e < RHALF(e_hdr.e_shnum); e++) {
        if (RWORD(sHdr[e].sh_type) == SHT_PROGBITS) {
            if (!strcmp(&elf_str_table[RWORD(sHdr[e].sh_name)], ".prtos_hdr")) {
                if (RWORD(sHdr[e].sh_size) != sizeof(struct prtos_hdr)) error_printf("Malformed .prtos_hdr section");
                if (prtos_hdr) error_printf(".prtos_hdr section found twice");
                if (prtos_image_hdr) error_printf(".prtos_image_hdr section already found");
                DO_MALLOC(prtos_hdr, sizeof(struct prtos_hdr));
                lseek(fd_elf, RWORD(sHdr[e].sh_offset), SEEK_SET);
                DO_READ(fd_elf, prtos_hdr, RWORD(sHdr[e].sh_size));
                if ((RWORD(prtos_hdr->start_signature) != PRTOS_EXEC_HYP_MAGIC) && (RWORD(prtos_hdr->end_signature) != PRTOS_EXEC_HYP_MAGIC))
                    error_printf("Malformed .prtos_hdr structure");
                custom_file_table = prtos_hdr->custom_file_table;
                xef_hdr.num_of_custom_files = RWORD(prtos_hdr->num_of_custom_files);
                xef_hdr.prtos_image_hdr = (prtos_address_t)RWORD(sHdr[e].sh_addr);
                xef_hdr.flags &= ~XEF_TYPE_MASK;
                xef_hdr.flags |= XEF_TYPE_HYPERVISOR;
            } else if (!strcmp(&elf_str_table[RWORD(sHdr[e].sh_name)], ".prtos_image_hdr")) {
                if (RWORD(sHdr[e].sh_size) != sizeof(struct prtos_image_hdr)) error_printf("Malformed .prtos_image_hdr section");
                if (prtos_hdr) error_printf(".prtos_hdr section already found");
                if (prtos_image_hdr) error_printf(".prtos_image_hdr section found twice");
                DO_MALLOC(prtos_image_hdr, sizeof(struct prtos_image_hdr));
                lseek(fd_elf, RWORD(sHdr[e].sh_offset), SEEK_SET);
                DO_READ(fd_elf, prtos_image_hdr, RWORD(sHdr[e].sh_size));
                if ((RWORD(prtos_image_hdr->start_signature) != PRTOS_EXEC_PARTITION_MAGIC) &&
                    (RWORD(prtos_image_hdr->end_signature) != PRTOS_EXEC_PARTITION_MAGIC))
                    error_printf("Malformed .prtos_image_hdr structure");
                custom_file_table = prtos_image_hdr->custom_file_table;
                xef_hdr.num_of_custom_files = RWORD(prtos_image_hdr->num_of_custom_files);
                xef_hdr.prtos_image_hdr = (prtos_address_t)RWORD(sHdr[e].sh_addr);
                xef_hdr.flags &= ~XEF_TYPE_MASK;
                xef_hdr.flags |= XEF_TYPE_PARTITION;
                xef_hdr.page_table = RWORD(prtos_image_hdr->page_table);
                xef_hdr.page_table_size = RWORD(prtos_image_hdr->page_table_size);
            }
        }
    }
    if (!prtos_hdr && !prtos_image_hdr) error_printf("Neither .prtos_hdr nor .prtos_image_hdr found");

    DO_MALLOC(xef_custom_file_table, sizeof(struct xef_custom_file) * xef_hdr.num_of_custom_files);
    for (e = 0; e < xef_hdr.num_of_custom_files; e++) {
        xef_custom_file_table[e].sAddr = RWORD(custom_file_table[e].sAddr);
        xef_custom_file_table[e].size = RWORD(custom_file_table[e].size);
    }

    pHdr = malloc(sizeof(ELF(Phdr)) * RHALF(e_hdr.e_phnum));
    lseek(fd_elf, RWORD(e_hdr.e_phoff), SEEK_SET);
    DO_READ(fd_elf, pHdr, sizeof(ELF(Phdr)) * RHALF(e_hdr.e_phnum));

    for (e = 0; e < RHALF(e_hdr.e_phnum); e++) {
        if (RWORD(pHdr[e].p_type) != PT_LOAD) continue;
        if (!pHdr[e].p_filesz) continue;
        xef_hdr.num_of_segments++;
        DO_REALLOC(xef_segment_table, xef_hdr.num_of_segments * sizeof(struct xef_segment));
        xef_sect = &xef_segment_table[xef_hdr.num_of_segments - 1];
        xef_sect->phys_addr = RWORD(pHdr[e].p_paddr);
        xef_sect->virt_addr = RWORD(pHdr[e].p_vaddr);
        xef_sect->file_size = RWORD(pHdr[e].p_filesz);
        xef_sect->deflated_file_size = RWORD(pHdr[e].p_filesz);

        xef_sect->offset = xef_hdr.image_length;
        xef_hdr.image_length += xef_sect->file_size;
        DO_REALLOC(image, xef_hdr.image_length);

        lseek(fd_elf, RWORD(pHdr[e].p_offset), SEEK_SET);
        DO_READ(fd_elf, &image[xef_sect->offset], RWORD(pHdr[e].p_filesz));
    }
    xef_hdr.deflated_image_length = xef_hdr.image_length;

    return 0;
}

static void parse_custom_file(int fdIn) {
    xef_hdr.num_of_segments = 1;
    xef_segment_table = 0;
    xef_custom_file_table = 0;
    xef_rel_table = 0;
    xef_rela_table = 0;
    image = 0;
    xef_hdr.image_length = 0;
    xef_hdr.flags &= ~XEF_TYPE_MASK;
    xef_hdr.flags |= XEF_TYPE_CUSTOMFILE;

    // An prtosImage has only one segment
    DO_REALLOC(xef_segment_table, xef_hdr.num_of_segments * sizeof(struct xef_segment));
    xef_hdr.num_of_custom_files = 0;

    xef_segment_table[0].phys_addr = 0;
    xef_segment_table[0].virt_addr = 0;
    xef_segment_table[0].file_size = lseek(fdIn, 0, SEEK_END);
    xef_segment_table[0].deflated_file_size = xef_segment_table[0].file_size;
    xef_segment_table[0].offset = xef_hdr.image_length;
    xef_hdr.image_length += xef_segment_table[0].file_size;
    DO_REALLOC(image, xef_hdr.image_length);
    lseek(fdIn, 0, SEEK_SET);
    DO_READ(fdIn, image, xef_segment_table[0].file_size);
    xef_hdr.deflated_image_length = xef_hdr.image_length;
}

static prtos_s32_t c_read(void *b, prtos_u_size_t s, void *d) {
    memcpy(b, *(prtos_u8_t **)d, s);
    *(prtos_u8_t **)d += s;
    return s;
}

static prtos_s32_t c_write(void *b, prtos_u_size_t s, void *d) {
    memcpy(*(prtos_u8_t **)d, b, s);
    *(prtos_u8_t **)d += s;
    return s;
}

static void c_seek(prtos_s_size_t offset, void *d) {
    *(prtos_u8_t **)d += offset;
}

static void compress_image(void) {
    prtos_u32_t cLen, len;
    prtos_u8_t *cImg;
    prtos_s32_t e, size;
    size = xef_hdr.image_length * 3;
    DO_MALLOC(cImg, size);
    for (e = 0, cLen = 0, len = 0; e < xef_hdr.num_of_segments; e++) {
        prtos_u8_t *ptrImg = &image[len], *ptrCImg = &cImg[cLen];
        if ((xef_segment_table[e].deflated_file_size = compress(xef_segment_table[e].file_size, size, c_read, &ptrImg, c_write, &ptrCImg, c_seek)) <= 0)
            error_printf("Unable to perform compression\n");
        xef_segment_table[e].offset = cLen;
        cLen += xef_segment_table[e].deflated_file_size;
        len += xef_segment_table[e].file_size;
        size -= cLen;
    }
    xef_hdr.deflated_image_length = cLen;
    free(image);
    image = cImg;
}

static void write_xef_image(char *output, int fd_out) {
    int pos = 0, e, file_size;
    struct digest_ctx digest_state;
    prtos_u8_t *img;

    pos = sizeof(struct xef_hdr);
    pos = ALIGNTO(pos, 8);
    xef_hdr.segment_table_offset = pos;
    for (e = 0; e < xef_hdr.num_of_segments; e++) {
        xef_segment_table[e].phys_addr = RWORD(xef_segment_table[e].phys_addr);
        xef_segment_table[e].virt_addr = RWORD(xef_segment_table[e].virt_addr);
        xef_segment_table[e].file_size = RWORD(xef_segment_table[e].file_size);
        xef_segment_table[e].deflated_file_size = RWORD(xef_segment_table[e].deflated_file_size);
        xef_segment_table[e].offset = RWORD(xef_segment_table[e].offset);
    }

    lseek(fd_out, xef_hdr.segment_table_offset, SEEK_SET);
    DO_WRITE(fd_out, xef_segment_table, sizeof(struct xef_segment) * xef_hdr.num_of_segments);

    pos += sizeof(struct xef_segment) * xef_hdr.num_of_segments;
    pos = ALIGNTO(pos, 8);
    xef_hdr.custom_file_table_offset = pos;
    for (e = 0; e < xef_hdr.num_of_custom_files; e++) {
        xef_custom_file_table[e].sAddr = RWORD(xef_custom_file_table[e].sAddr);
        xef_custom_file_table[e].size = RWORD(xef_custom_file_table[e].size);
    }
    lseek(fd_out, xef_hdr.custom_file_table_offset, SEEK_SET);
    DO_WRITE(fd_out, xef_custom_file_table, sizeof(struct xef_custom_file) * xef_hdr.num_of_custom_files);
    pos += sizeof(struct xef_custom_file) * xef_hdr.num_of_custom_files;
    pos = ALIGNTO(pos, 8);
    xef_hdr.image_offset = pos;
    lseek(fd_out, xef_hdr.image_offset, SEEK_SET);
    DO_WRITE(fd_out, image, xef_hdr.deflated_image_length);

    xef_hdr.flags |= XEF_DIGEST;
    xef_hdr.flags = RWORD(xef_hdr.flags);
    file_size = lseek(fd_out, 0, SEEK_END);
    if (file_size & 3) {  // Filling the xef with padding
        prtos_u32_t padding = 0;
        DO_WRITE(fd_out, &padding, 4 - (file_size & 3));
        file_size = lseek(fd_out, 0, SEEK_END);
    }

    xef_hdr.file_size = RWORD(file_size);
    xef_hdr.segment_table_offset = RWORD(xef_hdr.segment_table_offset);
    xef_hdr.num_of_segments = RWORD(xef_hdr.num_of_segments);
    xef_hdr.image_length = RWORD(xef_hdr.image_length);
    xef_hdr.deflated_image_length = RWORD(xef_hdr.deflated_image_length);
    xef_hdr.custom_file_table_offset = RWORD(xef_hdr.custom_file_table_offset);
    xef_hdr.num_of_custom_files = RWORD(xef_hdr.num_of_custom_files);
    xef_hdr.image_offset = RWORD(xef_hdr.image_offset);
    xef_hdr.prtos_image_hdr = RWORD(xef_hdr.prtos_image_hdr);
    xef_hdr.page_table = RWORD(xef_hdr.page_table);
    xef_hdr.page_table_size = RWORD(xef_hdr.page_table_size);
    lseek(fd_out, 0, SEEK_SET);
    DO_WRITE(fd_out, &xef_hdr, sizeof(xef_hdr));

    DO_MALLOC(img, file_size);
    lseek(fd_out, 0, SEEK_SET);
    DO_READ(fd_out, img, file_size);
    digest_init(&digest_state);
    digest_update(&digest_state, img, file_size);
    digest_final(xef_hdr.digest, &digest_state);
    free(img);
    for (e = 0; e < PRTOS_DIGEST_BYTES; e++) fprintf(stderr, "%02x", xef_hdr.digest[e]);

    fprintf(stderr, " %s\n", output);

    lseek(fd_out, 0, SEEK_SET);
    DO_WRITE(fd_out, &xef_hdr, sizeof(xef_hdr));
}

static int do_build(int argc, char **argv) {
    int fdIn, fd_out, opt, compressed = 0, customFile = 0;
    char *output, stdOutput[] = "a.out", *payload = 0;
    prtos_u32_t signature;
    output = stdOutput;

    while ((opt = getopt(argc, argv, "o:cp:m")) != -1) {
        switch (opt) {
            case 'o':
                DO_MALLOC(output, strlen(optarg) + 1);
                strcpy(output, optarg);
                break;
            case 'c':
                compressed = 1;
                break;
            case 'p':
                DO_MALLOC(payload, strlen(optarg) + 1);
                strcpy(payload, optarg);
                break;
            case 'm':
                customFile = 1;
                break;
            default: /* ? */
                fprintf(stderr, USAGE);
                return -2;
        }
    }

    if ((argc - optind) != 1) {
        fprintf(stderr, USAGE);
        return -2;
    }

    if ((fdIn = open(argv[optind], O_RDONLY)) < 0) error_printf("Unable to open %s\n", argv[optind]);

    memset(&xef_hdr, 0, sizeof(struct xef_hdr));
    xef_hdr.signature = RWORD(XEF_SIGNATURE);
    xef_hdr.version = RWORD(PRTOS_SET_VERSION(XEF_VERSION, XEF_SUBVERSION, XEF_REVISION));

    if (customFile)
        parse_custom_file(fdIn);
    else {
        DO_READ(fdIn, &signature, sizeof(prtos_u32_t));
        switch (signature) {
            case ELFMAG3 << 24 | ELFMAG2 << 16 | ELFMAG1 << 8 | ELFMAG0:
                parse_elf_image(fdIn);
                break;

            default:
                error_printf("Signature 0x%x unknown\n", signature);
        }
    }
    if (payload) {
        int e, fd_payload;
        if ((fd_payload = open(payload, O_RDONLY)) < 0) error_printf("Unable to open payload file %s\n", payload);
        read(fd_payload, &xef_hdr.payload, PRTOS_PAYLOAD_BYTES);
        close(fd_payload);
        for (e = 0; e < PRTOS_PAYLOAD_BYTES; e++)
            if (!((e + 1) % 8))
                fprintf(stderr, "%02x\n", xef_hdr.payload[e]);
            else
                fprintf(stderr, "%02x ", xef_hdr.payload[e]);
    }

    if (compressed) {
        compress_image();
        xef_hdr.flags |= XEF_COMPRESSED;
    }

    if ((fd_out = open(output, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP)) < 0) error_printf("Unable to open %s\n", output);

    write_xef_image(output, fd_out);
    return 0;
}

#define TAB "  "

static void print_header(void) {
    int e;
    fprintf(stderr, "XEF header:\n");
    fprintf(stderr, TAB "signature: 0x%x\n", RWORD(xef_hdr_read->signature));
    fprintf(stderr, TAB "version: %d.%d.%d\n", PRTOS_GET_VERSION(RWORD(xef_hdr_read->version)), PRTOS_GET_SUBVERSION(RWORD(xef_hdr_read->version)),
            PRTOS_GET_REVISION(RWORD(xef_hdr_read->version)));
    fprintf(stderr, TAB "flags: ");
    if (RWORD(xef_hdr_read->flags) & XEF_DIGEST) fprintf(stderr, "XEF_DIGEST ");
    if (RWORD(xef_hdr_read->flags) & XEF_COMPRESSED) fprintf(stderr, "XEF_COMPRESSED ");

    switch (RWORD(xef_hdr_read->flags) & XEF_TYPE_MASK) {
        case XEF_TYPE_HYPERVISOR:
            fprintf(stderr, "XEF_TYPE_HYPERVISOR ");
            break;
        case XEF_TYPE_PARTITION:
            fprintf(stderr, "XEF_TYPE_PARTITION ");
            break;
        case XEF_TYPE_CUSTOMFILE:
            fprintf(stderr, "XEF_TYPE_CUSTOMFILE ");
            break;
    }
    switch (RWORD(xef_hdr_read->flags) & XEF_ARCH_MASK) {
        case XEF_ARCH_SPARCv8:
            fprintf(stderr, "XEF_ARCH_SPARCv8 ");
            break;
    }
    fprintf(stderr, "\n");
    if (RWORD(xef_hdr_read->flags) & XEF_DIGEST) {
        fprintf(stderr, TAB "digest: ");
        for (e = 0; e < PRTOS_DIGEST_BYTES; e++) fprintf(stderr, "%02x", xef_hdr_read->digest[e]);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, TAB "payload: ");
    for (e = 0; e < PRTOS_PAYLOAD_BYTES; e++)
        if ((e + 1) != PRTOS_PAYLOAD_BYTES && !((e + 1) % 8))
            fprintf(stderr, "%02x\n" TAB "         ", xef_hdr_read->payload[e]);
        else
            fprintf(stderr, "%02x ", xef_hdr_read->payload[e]);
    fprintf(stderr, "\n");
    fprintf(stderr, TAB "file size: %" PRINT_PREF "u\n", RWORD(xef_hdr_read->file_size));
    fprintf(stderr, TAB "segment table offset: %" PRINT_PREF "u\n", RWORD(xef_hdr_read->segment_table_offset));
    fprintf(stderr, TAB "no. segments: %d\n", RWORD(xef_hdr_read->num_of_segments));
    fprintf(stderr, TAB "customFile table offset: %" PRINT_PREF "u\n", RWORD(xef_hdr_read->custom_file_table_offset));
    fprintf(stderr, TAB "no. customFiles: %d\n", RWORD(xef_hdr_read->num_of_custom_files));
    fprintf(stderr, TAB "image offset: %" PRINT_PREF "d\n", RWORD(xef_hdr_read->image_offset));
    fprintf(stderr, TAB "image length: %" PRINT_PREF "d\n", RWORD(xef_hdr_read->image_length));
    if (RWORD(xef_hdr_read->flags) & XEF_TYPE_PARTITION)
        fprintf(stderr, TAB "page table: [0x%" PRINT_PREF "x - 0x%" PRINT_PREF "x]\n", RWORD(xef_hdr_read->page_table),
                RWORD(xef_hdr_read->page_table) + RWORD(xef_hdr_read->page_table_size));
    fprintf(stderr, TAB "prtos image's header: 0x%" PRINT_PREF "x\n", RWORD(xef_hdr_read->prtos_image_hdr));
    fprintf(stderr, TAB "entry point: 0x%" PRINT_PREF "x\n", RWORD(xef_hdr_read->entry_point));
    if (RWORD(xef_hdr_read->flags) & XEF_COMPRESSED)
        fprintf(stderr, TAB "compressed image length: %" PRINT_PREF "d (%.2f%%)\n", RWORD(xef_hdr_read->deflated_image_length),
                100.0 * (float)RWORD(xef_hdr_read->deflated_image_length) / (float)RWORD(xef_hdr_read->image_length));
}

static void print_segments(void) {
    int e;
    fprintf(stderr, "Segment table: %d segments\n", RWORD(xef_hdr_read->num_of_segments));
    for (e = 0; e < RWORD(xef_hdr_read->num_of_segments); e++) {
        fprintf(stderr, TAB "segment %d\n", e);
        fprintf(stderr, TAB TAB "physical address: 0x%x\n", RWORD(xef_segment_table[e].phys_addr));
        fprintf(stderr, TAB TAB "virtual address: 0x%" PRINT_PREF "x\n", RWORD(xef_segment_table[e].virt_addr));
        fprintf(stderr, TAB TAB "file size: %" PRINT_PREF "d\n", RWORD(xef_segment_table[e].file_size));
        if (RWORD(xef_hdr_read->flags) & XEF_COMPRESSED)
            fprintf(stderr, TAB TAB "compressed file size: %" PRINT_PREF "d (%.2f%%)\n", RWORD(xef_segment_table[e].deflated_file_size),
                    100.0 * (float)RWORD(xef_segment_table[e].deflated_file_size) / (float)RWORD(xef_segment_table[e].file_size));
    }
}

static void print_custom_files(void) {
    int e;
    fprintf(stderr, "CustomFile table: %d customFiles\n", RWORD(xef_hdr_read->num_of_custom_files));
    for (e = 0; e < RWORD(xef_hdr_read->num_of_custom_files); e++) {
        fprintf(stderr, TAB "customFile %d\n", e);
        fprintf(stderr, TAB TAB "address: 0x%" PRINT_PREF "x\n", RWORD(xef_custom_file_table[e].sAddr));
        if (!RWORD(xef_custom_file_table[e].size))
            fprintf(stderr, TAB TAB "undefined file size\n");
        else
            fprintf(stderr, TAB TAB "file size: %" PRINT_PREF "d\n", RWORD(xef_custom_file_table[e].size));
    }
}

static void parse_xef(char *file, int fdIn) {
    struct digest_ctx digest_state;
    int file_size, e;
    unsigned char *img;
    static prtos_u8_t digest[PRTOS_DIGEST_BYTES];

    file_size = lseek(fdIn, 0, SEEK_END);
    DO_MALLOC(img, file_size);
    lseek(fdIn, 0, SEEK_SET);
    DO_READ(fdIn, img, file_size);
    xef_hdr_read = (struct xef_hdr *)img;
    xef_segment_table = (struct xef_segment *)(img + RWORD(xef_hdr_read->segment_table_offset));
    xef_custom_file_table = (struct xef_custom_file *)(img + RWORD(xef_hdr_read->custom_file_table_offset));
    if (xef_hdr_read->signature != RWORD(XEF_SIGNATURE)) error_printf("Not a XEF file. Wrong signature (0x%x)\n", xef_hdr_read->signature);
    if (xef_hdr_read->file_size != RWORD(file_size)) error_printf("Wrong file sile %d - expected %d\n", RWORD(xef_hdr_read->file_size), file_size);
    digest_init(&digest_state);
    digest_update(&digest_state, img, offsetof(struct xef_hdr, digest));
    digest_update(&digest_state, (prtos_u8_t *)digest, PRTOS_DIGEST_BYTES);
    digest_update(&digest_state, &img[offsetof(struct xef_hdr, payload)], file_size - offsetof(struct xef_hdr, payload));
    digest_final(digest, &digest_state);
    for (e = 0; e < PRTOS_DIGEST_BYTES; e++)
        if (digest[e] != xef_hdr_read->digest[e]) error_printf("Wrong digest - file corrupted?\n");

    if ((RWORD(xef_hdr_read->segment_table_offset) > file_size) ||
        ((RWORD(xef_hdr_read->segment_table_offset) + RWORD(xef_hdr_read->num_of_segments) * sizeof(struct xef_segment)) > file_size))
        error_printf("Segment table beyond file?\n");
    if ((RWORD(xef_hdr_read->image_offset) > file_size) || ((RWORD(xef_hdr_read->image_offset) + RWORD(xef_hdr_read->deflated_image_length)) > file_size))
        error_printf("Image beyond file?\n");
    if ((RWORD(xef_hdr_read->custom_file_table_offset) > file_size) ||
        ((RWORD(xef_hdr_read->custom_file_table_offset) + RWORD(xef_hdr_read->num_of_custom_files) * sizeof(struct xef_custom_file)) > file_size))
        error_printf("CustomFile table beyond file?\n");
}

static int do_read(int argc, char **argv) {
    int header = 0, fdIn, opt, segments = 0, customFiles = 0;

    if (argc < 2) {
        fprintf(stderr, USAGE);
        return -2;
    }

    while ((opt = getopt(argc, argv, "hsm")) != -1) {
        switch (opt) {
            case 'h':
                header = 1;
                break;
            case 's':
                segments = 1;
                break;
            case 'm':
                customFiles = 1;
                break;
            default: /* ? */
                fprintf(stderr, USAGE);
                return -2;
        }
    }

    if ((argc - optind) != 1) {
        fprintf(stderr, USAGE);
        return -2;
    }

    if ((fdIn = open(argv[optind], O_RDONLY)) < 0) error_printf("Unable to open %s\n", argv[optind]);

    parse_xef(argv[optind], fdIn);
    if (!header && !segments && !customFiles) {
        fprintf(stderr, USAGE);
        return -2;
    }

    if (header) print_header();
    if (segments) print_segments();
    if (customFiles) print_custom_files();
    close(fdIn);

    return 0;
}

int main(int argc, char **argv) {
    char **_argv;
    int e, i;

    if (argc < 2) {
        fprintf(stderr, USAGE);
        return -2;
    }

    _argv = malloc(sizeof(char *) * (argc - 1));
    for (e = 0, i = 0; e < argc; e++) {
        if (e == 1) continue;
        _argv[i] = strdup(argv[e]);
        i++;
    }

    if (!strcasecmp(argv[1], "read")) {
        do_read(argc - 1, _argv);
        exit(0);
    }
    if (!strcasecmp(argv[1], "build")) {
        do_build(argc - 1, _argv);
        exit(0);
    }

    fprintf(stderr, USAGE);
    return 0;
}
