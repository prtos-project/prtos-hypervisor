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
#include <peftool.h>

#include <prtos_inc/digest.h>
#include <prtos_inc/compress.h>
#include <prtos_inc/arch/arch_types.h>
#include <prtos_inc/prtosef.h>

#define TOOL_NAME "prtoseformat"
#define USAGE                               \
    "Usage: " TOOL_NAME " [read] [build]\n" \
    " \tread [-h|-s|-m] <input>\n"          \
    " \tbuild [-m] [-o <output>] [-c] [-p <payload_file>] <input>\n"

struct pef_hdr pef_hdr, *pef_hdr_read;
struct pef_segment *pef_segment_table;
struct pef_custom_file *pef_custom_file_table;
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

#if defined(CONFIG_AARCH64)
#define EM_ARCH EM_AARCH64  // 64-bit ARM architecture identifier
#define ELF(x) Elf64_##x
#define PRINT_PREF
#endif

static int parse_elf_image(int fd_elf) {
    struct prtos_image_hdr *prtos_image_hdr = 0;
    struct pef_segment *pef_sect;
    struct prtos_hdr *prtos_hdr = 0;
    struct pef_custom_file *custom_file_table;
    ELF(Ehdr) e_hdr;
    ELF(Phdr) * phdr;
    ELF(Shdr) * shdr;
    char *elf_str_table = 0;
    int e;

    custom_file_table = 0;
    pef_hdr.num_of_segments = 0;
    pef_segment_table = 0;
    pef_custom_file_table = 0;
    image = 0;
    pef_hdr.image_length = 0;

    lseek(fd_elf, 0, SEEK_SET);
    DO_READ(fd_elf, &e_hdr, sizeof(ELF(Ehdr)));

    if ((RHALF(e_hdr.e_type) != ET_EXEC) || (e_hdr.e_machine != RHALF(EM_ARCH)) || (e_hdr.e_phentsize != RHALF(sizeof(ELF(Phdr)))))
        error_printf("Malformed ELF header");

    // Looking for .prtos_hdr/.prtos_image_hdr sections
    shdr = malloc(sizeof(ELF(Shdr)) * RHALF(e_hdr.e_shnum));
    lseek(fd_elf, RWORD(e_hdr.e_shoff), SEEK_SET);
    DO_READ(fd_elf, shdr, sizeof(ELF(Shdr)) * RHALF(e_hdr.e_shnum));

// Locating the string table
#if (__GNUC__ >= 4 && __GNUC__ < 5)
    for (e = 0; e < RHALF(e_hdr.e_shnum); e++)
        if (RWORD(shdr[e].sh_type) == SHT_STRTAB) {
            DO_MALLOC(elf_str_table, RWORD(shdr[e].sh_size));
            lseek(fd_elf, RWORD(shdr[e].sh_offset), SEEK_SET);
            DO_READ(fd_elf, elf_str_table, RWORD(shdr[e].sh_size));
            break;
        }
#else
    ELF(Shdr) SHSTRTAB;
    lseek(fd_elf, 0, SEEK_SET);
    lseek(fd_elf, e_hdr.e_shoff + (e_hdr.e_shentsize * e_hdr.e_shstrndx), SEEK_SET);
    DO_READ(fd_elf, &SHSTRTAB, e_hdr.e_shentsize);
    lseek(fd_elf, SHSTRTAB.sh_offset, SEEK_SET);
    DO_MALLOC(elf_str_table, RWORD(SHSTRTAB.sh_size));
    DO_READ(fd_elf, elf_str_table, RWORD(SHSTRTAB.sh_size));
#endif

    if (!elf_str_table) error_printf("ELF string table not found");

    pef_hdr.entry_point = e_hdr.e_entry;
    for (e = 0; e < RHALF(e_hdr.e_shnum); e++) {
        if (RWORD(shdr[e].sh_type) == SHT_PROGBITS) {
            if (!strcmp(&elf_str_table[RWORD(shdr[e].sh_name)], ".prtos_hdr")) {
                if (RWORD(shdr[e].sh_size) != sizeof(struct prtos_hdr)) error_printf("Malformed .prtos_hdr section");
                if (prtos_hdr) error_printf(".prtos_hdr section found twice");
                if (prtos_image_hdr) error_printf(".prtos_image_hdr section already found");
                DO_MALLOC(prtos_hdr, sizeof(struct prtos_hdr));
                lseek(fd_elf, RWORD(shdr[e].sh_offset), SEEK_SET);
                DO_READ(fd_elf, prtos_hdr, RWORD(shdr[e].sh_size));
                if ((RWORD(prtos_hdr->start_signature) != PRTOS_EXEC_HYP_MAGIC) && (RWORD(prtos_hdr->end_signature) != PRTOS_EXEC_HYP_MAGIC))
                    error_printf("Malformed .prtos_hdr structure");
                custom_file_table = prtos_hdr->custom_file_table;
                pef_hdr.num_of_custom_files = RWORD(prtos_hdr->num_of_custom_files);
                pef_hdr.prtos_image_hdr = (prtos_address_t)RWORD(shdr[e].sh_addr);
                pef_hdr.flags &= ~PEF_TYPE_MASK;
                pef_hdr.flags |= PEF_TYPE_HYPERVISOR;
            } else if (!strcmp(&elf_str_table[RWORD(shdr[e].sh_name)], ".prtos_image_hdr")) {
                if (RWORD(shdr[e].sh_size) != sizeof(struct prtos_image_hdr)) error_printf("Malformed .prtos_image_hdr section");
                if (prtos_hdr) error_printf(".prtos_hdr section already found");
                if (prtos_image_hdr) error_printf(".prtos_image_hdr section found twice");
                DO_MALLOC(prtos_image_hdr, sizeof(struct prtos_image_hdr));
                lseek(fd_elf, RWORD(shdr[e].sh_offset), SEEK_SET);
                DO_READ(fd_elf, prtos_image_hdr, RWORD(shdr[e].sh_size));
                if ((RWORD(prtos_image_hdr->start_signature) != PRTOS_EXEC_PARTITION_MAGIC) &&
                    (RWORD(prtos_image_hdr->end_signature) != PRTOS_EXEC_PARTITION_MAGIC))
                    error_printf("Malformed .prtos_image_hdr structure");
                custom_file_table = prtos_image_hdr->custom_file_table;
                pef_hdr.num_of_custom_files = RWORD(prtos_image_hdr->num_of_custom_files);
                pef_hdr.prtos_image_hdr = (prtos_address_t)RWORD(shdr[e].sh_addr);
                pef_hdr.flags &= ~PEF_TYPE_MASK;
                pef_hdr.flags |= PEF_TYPE_PARTITION;
                pef_hdr.page_table = RWORD(prtos_image_hdr->page_table);
                pef_hdr.page_table_size = RWORD(prtos_image_hdr->page_table_size);
            }
        }
    }

    if (!prtos_hdr && !prtos_image_hdr) error_printf("Neither .prtos_hdr nor .prtos_image_hdr found");

    DO_MALLOC(pef_custom_file_table, sizeof(struct pef_custom_file) * pef_hdr.num_of_custom_files);
    for (e = 0; e < pef_hdr.num_of_custom_files; e++) {
        pef_custom_file_table[e].start_addr = RWORD(custom_file_table[e].start_addr);
        pef_custom_file_table[e].size = RWORD(custom_file_table[e].size);
    }

    phdr = malloc(sizeof(ELF(Phdr)) * RHALF(e_hdr.e_phnum));
    lseek(fd_elf, RWORD(e_hdr.e_phoff), SEEK_SET);
    DO_READ(fd_elf, phdr, sizeof(ELF(Phdr)) * RHALF(e_hdr.e_phnum));

    for (e = 0; e < RHALF(e_hdr.e_phnum); e++) {
        if (RWORD(phdr[e].p_type) != PT_LOAD) continue;
        if (!phdr[e].p_filesz) continue;
        pef_hdr.num_of_segments++;
        DO_REALLOC(pef_segment_table, pef_hdr.num_of_segments * sizeof(struct pef_segment));
        pef_sect = &pef_segment_table[pef_hdr.num_of_segments - 1];
        pef_sect->phys_addr = RWORD(phdr[e].p_paddr);
        pef_sect->virt_addr = RWORD(phdr[e].p_vaddr);
        pef_sect->file_size = RWORD(phdr[e].p_filesz);
        pef_sect->deflated_file_size = RWORD(phdr[e].p_filesz);

        pef_sect->offset = pef_hdr.image_length;
        pef_hdr.image_length += pef_sect->file_size;
        DO_REALLOC(image, pef_hdr.image_length);

        lseek(fd_elf, RWORD(phdr[e].p_offset), SEEK_SET);
        DO_READ(fd_elf, &image[pef_sect->offset], RWORD(phdr[e].p_filesz));
    }
    pef_hdr.deflated_image_length = pef_hdr.image_length;

    return 0;
}

static void parse_custom_file(int fd_in) {
    pef_hdr.num_of_segments = 1;
    pef_segment_table = 0;
    pef_custom_file_table = 0;
    image = 0;
    pef_hdr.image_length = 0;
    pef_hdr.flags &= ~PEF_TYPE_MASK;
    pef_hdr.flags |= PEF_TYPE_CUSTOMFILE;

    // An prtosImage has only one segment
    DO_REALLOC(pef_segment_table, pef_hdr.num_of_segments * sizeof(struct pef_segment));
    pef_hdr.num_of_custom_files = 0;

    pef_segment_table[0].phys_addr = 0;
    pef_segment_table[0].virt_addr = 0;
    pef_segment_table[0].file_size = lseek(fd_in, 0, SEEK_END);
    pef_segment_table[0].deflated_file_size = pef_segment_table[0].file_size;
    pef_segment_table[0].offset = pef_hdr.image_length;
    pef_hdr.image_length += pef_segment_table[0].file_size;
    DO_REALLOC(image, pef_hdr.image_length);
    lseek(fd_in, 0, SEEK_SET);
    DO_READ(fd_in, image, pef_segment_table[0].file_size);
    pef_hdr.deflated_image_length = pef_hdr.image_length;
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
    prtos_u32_t c_len, len;
    prtos_u8_t *cImg;
    prtos_s32_t e, size;
    size = pef_hdr.image_length * 3;
    DO_MALLOC(cImg, size);
    for (e = 0, c_len = 0, len = 0; e < pef_hdr.num_of_segments; e++) {
        prtos_u8_t *ptr_img = &image[len], *ptrCImg = &cImg[c_len];
        if ((pef_segment_table[e].deflated_file_size = compress(pef_segment_table[e].file_size, size, c_read, &ptr_img, c_write, &ptrCImg, c_seek)) <= 0)
            error_printf("Unable to perform compression\n");
        pef_segment_table[e].offset = c_len;
        c_len += pef_segment_table[e].deflated_file_size;
        len += pef_segment_table[e].file_size;
        size -= c_len;
    }
    pef_hdr.deflated_image_length = c_len;
    free(image);
    image = cImg;
}

static void write_pef_image(char *output, int fd_out) {
    int pos = 0, e, file_size;
    struct digest_ctx digest_state;
    prtos_u8_t *img;

    pos = sizeof(struct pef_hdr);
    pos = ALIGNTO(pos, 8);
    pef_hdr.segment_table_offset = pos;
    for (e = 0; e < pef_hdr.num_of_segments; e++) {
        pef_segment_table[e].phys_addr = RWORD(pef_segment_table[e].phys_addr);
        pef_segment_table[e].virt_addr = RWORD(pef_segment_table[e].virt_addr);
        pef_segment_table[e].file_size = RWORD(pef_segment_table[e].file_size);
        pef_segment_table[e].deflated_file_size = RWORD(pef_segment_table[e].deflated_file_size);
        pef_segment_table[e].offset = RWORD(pef_segment_table[e].offset);
    }

    lseek(fd_out, pef_hdr.segment_table_offset, SEEK_SET);
    DO_WRITE(fd_out, pef_segment_table, sizeof(struct pef_segment) * pef_hdr.num_of_segments);

    pos += sizeof(struct pef_segment) * pef_hdr.num_of_segments;
    pos = ALIGNTO(pos, 8);
    pef_hdr.custom_file_table_offset = pos;
    for (e = 0; e < pef_hdr.num_of_custom_files; e++) {
        pef_custom_file_table[e].start_addr = RWORD(pef_custom_file_table[e].start_addr);
        pef_custom_file_table[e].size = RWORD(pef_custom_file_table[e].size);
    }
    lseek(fd_out, pef_hdr.custom_file_table_offset, SEEK_SET);
    DO_WRITE(fd_out, pef_custom_file_table, sizeof(struct pef_custom_file) * pef_hdr.num_of_custom_files);
    pos += sizeof(struct pef_custom_file) * pef_hdr.num_of_custom_files;
    pos = ALIGNTO(pos, 8);
    pef_hdr.image_offset = pos;
    lseek(fd_out, pef_hdr.image_offset, SEEK_SET);
    DO_WRITE(fd_out, image, pef_hdr.deflated_image_length);

    pef_hdr.flags |= PEF_DIGEST;
    pef_hdr.flags = RWORD(pef_hdr.flags);
    file_size = lseek(fd_out, 0, SEEK_END);
    if (file_size & 3) {  // Filling the pef with padding
        prtos_u32_t padding = 0;
        DO_WRITE(fd_out, &padding, 4 - (file_size & 3));
        file_size = lseek(fd_out, 0, SEEK_END);
    }

    pef_hdr.file_size = RWORD(file_size);
    pef_hdr.segment_table_offset = RWORD(pef_hdr.segment_table_offset);
    pef_hdr.num_of_segments = RWORD(pef_hdr.num_of_segments);
    pef_hdr.image_length = RWORD(pef_hdr.image_length);
    pef_hdr.deflated_image_length = RWORD(pef_hdr.deflated_image_length);
    pef_hdr.custom_file_table_offset = RWORD(pef_hdr.custom_file_table_offset);
    pef_hdr.num_of_custom_files = RWORD(pef_hdr.num_of_custom_files);
    pef_hdr.image_offset = RWORD(pef_hdr.image_offset);
    pef_hdr.prtos_image_hdr = RWORD(pef_hdr.prtos_image_hdr);
    pef_hdr.page_table = RWORD(pef_hdr.page_table);
    pef_hdr.page_table_size = RWORD(pef_hdr.page_table_size);
    lseek(fd_out, 0, SEEK_SET);
    DO_WRITE(fd_out, &pef_hdr, sizeof(pef_hdr));

    DO_MALLOC(img, file_size);
    lseek(fd_out, 0, SEEK_SET);
    DO_READ(fd_out, img, file_size);
    digest_init(&digest_state);
    digest_update(&digest_state, img, file_size);
    digest_final(pef_hdr.digest, &digest_state);
    free(img);
    for (e = 0; e < PRTOS_DIGEST_BYTES; e++) fprintf(stderr, "%02x", pef_hdr.digest[e]);

    fprintf(stderr, " %s\n", output);

    lseek(fd_out, 0, SEEK_SET);
    DO_WRITE(fd_out, &pef_hdr, sizeof(pef_hdr));
}

static int do_build(int argc, char **argv) {
    int fd_in, fd_out, opt, compressed = 0, custom_file = 0;
    char *output, std_output[] = "a.out", *payload = 0;
    prtos_u32_t signature;
    output = std_output;

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
                custom_file = 1;
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

    if ((fd_in = open(argv[optind], O_RDONLY)) < 0) error_printf("Unable to open %s\n", argv[optind]);

    memset(&pef_hdr, 0, sizeof(struct pef_hdr));
    pef_hdr.signature = RWORD(PEF_SIGNATURE);
    pef_hdr.version = RWORD(PRTOS_SET_VERSION(PEF_VERSION, PEF_SUBVERSION, PEF_REVISION));

    if (custom_file)
        parse_custom_file(fd_in);
    else {
        DO_READ(fd_in, &signature, sizeof(prtos_u32_t));
        switch (signature) {
            case ELFMAG3 << 24 | ELFMAG2 << 16 | ELFMAG1 << 8 | ELFMAG0:
                parse_elf_image(fd_in);
                break;

            default:
                error_printf("Signature 0x%x unknown\n", signature);
        }
    }
    if (payload) {
        int e, fd_payload;
        if ((fd_payload = open(payload, O_RDONLY)) < 0) error_printf("Unable to open payload file %s\n", payload);
        DO_READ(fd_payload, &pef_hdr.payload, PRTOS_PAYLOAD_BYTES);
        close(fd_payload);
        for (e = 0; e < PRTOS_PAYLOAD_BYTES; e++)
            if (!((e + 1) % 8))
                fprintf(stderr, "%02x\n", pef_hdr.payload[e]);
            else
                fprintf(stderr, "%02x ", pef_hdr.payload[e]);
    }

    if (compressed) {
        compress_image();
        pef_hdr.flags |= PEF_COMPRESSED;
    }

    if ((fd_out = open(output, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP)) < 0) error_printf("Unable to open %s\n", output);

    write_pef_image(output, fd_out);
    return 0;
}

#define TAB "  "

static void print_header(void) {
    int e;
    fprintf(stderr, "PEF header:\n");
    fprintf(stderr, TAB "signature: 0x%x\n", RWORD(pef_hdr_read->signature));
    fprintf(stderr, TAB "version: %d.%d.%d\n", PRTOS_GET_VERSION(RWORD(pef_hdr_read->version)), PRTOS_GET_SUBVERSION(RWORD(pef_hdr_read->version)),
            PRTOS_GET_REVISION(RWORD(pef_hdr_read->version)));
    fprintf(stderr, TAB "flags: ");
    if (RWORD(pef_hdr_read->flags) & PEF_DIGEST) fprintf(stderr, "PEF_DIGEST ");
    if (RWORD(pef_hdr_read->flags) & PEF_COMPRESSED) fprintf(stderr, "PEF_COMPRESSED ");

    switch (RWORD(pef_hdr_read->flags) & PEF_TYPE_MASK) {
        case PEF_TYPE_HYPERVISOR:
            fprintf(stderr, "PEF_TYPE_HYPERVISOR ");
            break;
        case PEF_TYPE_PARTITION:
            fprintf(stderr, "PEF_TYPE_PARTITION ");
            break;
        case PEF_TYPE_CUSTOMFILE:
            fprintf(stderr, "PEF_TYPE_CUSTOMFILE ");
            break;
    }
    switch (RWORD(pef_hdr_read->flags) & PEF_ARCH_MASK) {
        case PEF_ARCH_ARMV8:
            fprintf(stderr, "PEF_ARCH_ARMV8 ");
            break;
    }
    fprintf(stderr, "\n");
    if (RWORD(pef_hdr_read->flags) & PEF_DIGEST) {
        fprintf(stderr, TAB "digest: ");
        for (e = 0; e < PRTOS_DIGEST_BYTES; e++) fprintf(stderr, "%02x", pef_hdr_read->digest[e]);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, TAB "payload: ");
    for (e = 0; e < PRTOS_PAYLOAD_BYTES; e++)
        if ((e + 1) != PRTOS_PAYLOAD_BYTES && !((e + 1) % 8))
            fprintf(stderr, "%02x\n" TAB "         ", pef_hdr_read->payload[e]);
        else
            fprintf(stderr, "%02x ", pef_hdr_read->payload[e]);
    fprintf(stderr, "\n");
// TODO: I haven't figured out a unified way to print values without parse warnings both on 32-bit and 64-bit platforms, so here just use a WA.
#if defined(CONFIG_x86)
    fprintf(stderr, TAB "file size: %" PRINT_PREF "u\n", RWORD(pef_hdr_read->file_size));
    fprintf(stderr, TAB "segment table offset: %" PRINT_PREF "u\n", RWORD(pef_hdr_read->segment_table_offset));
    fprintf(stderr, TAB "no. segments: %d\n", RWORD(pef_hdr_read->num_of_segments));
    fprintf(stderr, TAB "custom_file table offset: %" PRINT_PREF "llu\n", RWORD(pef_hdr_read->custom_file_table_offset));
    fprintf(stderr, TAB "no. custom_files: %d\n", RWORD(pef_hdr_read->num_of_custom_files));
    fprintf(stderr, TAB "image offset: %" PRINT_PREF "lld\n", RWORD(pef_hdr_read->image_offset));
    fprintf(stderr, TAB "image length: %" PRINT_PREF "lld\n", RWORD(pef_hdr_read->image_length));
    if (RWORD(pef_hdr_read->flags) & PEF_TYPE_PARTITION)
        fprintf(stderr, TAB "page table: [0x%" PRINT_PREF "llx - 0x%" PRINT_PREF "llx]\n", RWORD(pef_hdr_read->page_table),
                RWORD(pef_hdr_read->page_table) + RWORD(pef_hdr_read->page_table_size));
    fprintf(stderr, TAB "prtos image's header: 0x%" PRINT_PREF "llx\n", RWORD(pef_hdr_read->prtos_image_hdr));
    fprintf(stderr, TAB "entry point: 0x%" PRINT_PREF "llx\n", RWORD(pef_hdr_read->entry_point));
    if (RWORD(pef_hdr_read->flags) & PEF_COMPRESSED)
        fprintf(stderr, TAB "compressed image length: %" PRINT_PREF "lld (%.2f%%)\n", RWORD(pef_hdr_read->deflated_image_length),
                100.0 * (float)RWORD(pef_hdr_read->deflated_image_length) / (float)RWORD(pef_hdr_read->image_length));
#else
    fprintf(stderr, TAB "file size: %" PRINT_PREF "llu\n", RWORD(pef_hdr_read->file_size));
    fprintf(stderr, TAB "segment table offset: %" PRINT_PREF "llu\n", RWORD(pef_hdr_read->segment_table_offset));
    fprintf(stderr, TAB "no. segments: %d\n", RWORD(pef_hdr_read->num_of_segments));
    fprintf(stderr, TAB "custom_file table offset: %" PRINT_PREF "llu\n", RWORD(pef_hdr_read->custom_file_table_offset));
    fprintf(stderr, TAB "no. custom_files: %d\n", RWORD(pef_hdr_read->num_of_custom_files));
    fprintf(stderr, TAB "image offset: %" PRINT_PREF "lld\n", RWORD(pef_hdr_read->image_offset));
    fprintf(stderr, TAB "image length: %" PRINT_PREF "lld\n", RWORD(pef_hdr_read->image_length));
    if (RWORD(pef_hdr_read->flags) & PEF_TYPE_PARTITION)
        fprintf(stderr, TAB "page table: [0x%" PRINT_PREF "llx - 0x%" PRINT_PREF "llx]\n", RWORD(pef_hdr_read->page_table),
                RWORD(pef_hdr_read->page_table) + RWORD(pef_hdr_read->page_table_size));
    fprintf(stderr, TAB "prtos image's header: 0x%" PRINT_PREF "llx\n", RWORD(pef_hdr_read->prtos_image_hdr));
    fprintf(stderr, TAB "entry point: 0x%" PRINT_PREF "llx\n", RWORD(pef_hdr_read->entry_point));
    if (RWORD(pef_hdr_read->flags) & PEF_COMPRESSED)
        fprintf(stderr, TAB "compressed image length: %" PRINT_PREF "lld (%.2f%%)\n", RWORD(pef_hdr_read->deflated_image_length),
                100.0 * (float)RWORD(pef_hdr_read->deflated_image_length) / (float)RWORD(pef_hdr_read->image_length));
#endif
}

static void print_segments(void) {
    int e;
    fprintf(stderr, "Segment table: %d segments\n", RWORD(pef_hdr_read->num_of_segments));
    for (e = 0; e < RWORD(pef_hdr_read->num_of_segments); e++) {
// TODO: I haven't figured out a unified way to print values without parse warnings both on 32-bit and 64-bit platforms, so here just use a WA.
#if defined(CONFIG_x86)
        fprintf(stderr, TAB TAB "physical address: 0x%x\n", RWORD(pef_segment_table[e].phys_addr));
        fprintf(stderr, TAB TAB "virtual address: 0x%" PRINT_PREF "x\n", RWORD(pef_segment_table[e].virt_addr));
        fprintf(stderr, TAB TAB "file size: %" PRINT_PREF "d\n", RWORD(pef_segment_table[e].file_size));
        if (RWORD(pef_hdr_read->flags) & PEF_COMPRESSED)
            fprintf(stderr, TAB TAB "compressed file size: %" PRINT_PREF "lld (%.2f%%)\n", RWORD(pef_segment_table[e].deflated_file_size),
                    100.0 * (float)RWORD(pef_segment_table[e].deflated_file_size) / (float)RWORD(pef_segment_table[e].file_size));
#else
        fprintf(stderr, TAB "segment %d\n", e);
        fprintf(stderr, TAB TAB "physical address: 0x%llx\n", RWORD(pef_segment_table[e].phys_addr));
        fprintf(stderr, TAB TAB "virtual address: 0x%" PRINT_PREF "llx\n", RWORD(pef_segment_table[e].virt_addr));
        fprintf(stderr, TAB TAB "file size: %" PRINT_PREF "lld\n", RWORD(pef_segment_table[e].file_size));
        if (RWORD(pef_hdr_read->flags) & PEF_COMPRESSED)
            fprintf(stderr, TAB TAB "compressed file size: %" PRINT_PREF "lld (%.2f%%)\n", RWORD(pef_segment_table[e].deflated_file_size),
                    100.0 * (float)RWORD(pef_segment_table[e].deflated_file_size) / (float)RWORD(pef_segment_table[e].file_size));

#endif
    }
}

static void print_custom_files(void) {
    int e;
    fprintf(stderr, "custom_file table: %d custom_files\n", RWORD(pef_hdr_read->num_of_custom_files));
    for (e = 0; e < RWORD(pef_hdr_read->num_of_custom_files); e++) {
        fprintf(stderr, TAB "custom_file %d\n", e);
// TODO: I haven't figured out a unified way to print values without parse warnings both on 32-bit and 64-bit platforms, so here just use a WA.
#if defined(CONFIG_x86)
        fprintf(stderr, TAB TAB "address: 0x%" PRINT_PREF "x\n", RWORD(pef_custom_file_table[e].start_addr));
        if (!RWORD(pef_custom_file_table[e].size))
            fprintf(stderr, TAB TAB "undefined file size\n");
        else
            fprintf(stderr, TAB TAB "file size: %" PRINT_PREF "d\n", RWORD(pef_custom_file_table[e].size));
#else
        fprintf(stderr, TAB TAB "address: 0x%" PRINT_PREF "llx\n", RWORD(pef_custom_file_table[e].start_addr));
        if (!RWORD(pef_custom_file_table[e].size))
            fprintf(stderr, TAB TAB "undefined file size\n");
        else
            fprintf(stderr, TAB TAB "file size: %" PRINT_PREF "lld\n", RWORD(pef_custom_file_table[e].size));
#endif
    }
}

static void parse_pef(char *file, int fd_in) {
    struct digest_ctx digest_state;
    int file_size, e;
    unsigned char *img;
    static prtos_u8_t digest[PRTOS_DIGEST_BYTES];

    file_size = lseek(fd_in, 0, SEEK_END);
    DO_MALLOC(img, file_size);
    lseek(fd_in, 0, SEEK_SET);
    DO_READ(fd_in, img, file_size);
    pef_hdr_read = (struct pef_hdr *)img;
    pef_segment_table = (struct pef_segment *)(img + RWORD(pef_hdr_read->segment_table_offset));
    pef_custom_file_table = (struct pef_custom_file *)(img + RWORD(pef_hdr_read->custom_file_table_offset));
    if (pef_hdr_read->signature != RWORD(PEF_SIGNATURE)) error_printf("Not a PEF file. Wrong signature (0x%x)\n", pef_hdr_read->signature);
    if (pef_hdr_read->file_size != RWORD(file_size)) error_printf("Wrong file sile %d - expected %d\n", RWORD(pef_hdr_read->file_size), file_size);
    digest_init(&digest_state);
    digest_update(&digest_state, img, offsetof(struct pef_hdr, digest));
    digest_update(&digest_state, (prtos_u8_t *)digest, PRTOS_DIGEST_BYTES);
    digest_update(&digest_state, &img[offsetof(struct pef_hdr, payload)], file_size - offsetof(struct pef_hdr, payload));
    digest_final(digest, &digest_state);
    for (e = 0; e < PRTOS_DIGEST_BYTES; e++)
        if (digest[e] != pef_hdr_read->digest[e]) error_printf("Wrong digest - file corrupted?\n");

    if ((RWORD(pef_hdr_read->segment_table_offset) > file_size) ||
        ((RWORD(pef_hdr_read->segment_table_offset) + RWORD(pef_hdr_read->num_of_segments) * sizeof(struct pef_segment)) > file_size))
        error_printf("Segment table beyond file?\n");
    if ((RWORD(pef_hdr_read->image_offset) > file_size) || ((RWORD(pef_hdr_read->image_offset) + RWORD(pef_hdr_read->deflated_image_length)) > file_size))
        error_printf("Image beyond file?\n");
    if ((RWORD(pef_hdr_read->custom_file_table_offset) > file_size) ||
        ((RWORD(pef_hdr_read->custom_file_table_offset) + RWORD(pef_hdr_read->num_of_custom_files) * sizeof(struct pef_custom_file)) > file_size))
        error_printf("custom_file table beyond file?\n");
}

static int do_read(int argc, char **argv) {
    int header = 0, fd_in, opt, segments = 0, custom_files = 0;

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
                custom_files = 1;
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

    if ((fd_in = open(argv[optind], O_RDONLY)) < 0) error_printf("Unable to open %s\n", argv[optind]);

    parse_pef(argv[optind], fd_in);
    if (!header && !segments && !custom_files) {
        fprintf(stderr, USAGE);
        return -2;
    }

    if (header) print_header();
    if (segments) print_segments();
    if (custom_files) print_custom_files();
    close(fd_in);

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
