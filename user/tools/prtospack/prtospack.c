/*
 * FILE: prtospack
 *
 * Create a pack holding the image of prtos and partitions to be written
 * into the ROM
 *
 * www.prtos.org
 */

#define _GNU_SOURCE

#include <unistd.h>
#include <ctype.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <features.h>
#include <prtos_inc/arch/arch_types.h>
#include <prtos_inc/prtosef.h>

#include <prtos_inc/compress.h>
#include <prtos_inc/digest.h>

#include <endianess.h>
#include <prtospack.h>

#define PRINT_PREF

static struct prtos_exec_container_hdr prtos_exec_container_hdr;
static prtos_u8_t *file_data = 0, *file_img = 0;
static prtos_u32_t file_data_length = 0;
static struct prtos_exec_file *prtos_exec_file_table = 0;

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

struct prtos_exec_partition *partition_table = 0, *hypervisor;

static struct file {
    char *name;
    unsigned int offset;
} *file_table = 0;

static int num_of_files = 0, num_of_partitions = 0;

static inline int add_file(char *file_name, unsigned int offset) {
    int p = num_of_files++;
    DO_REALLOC(file_table, num_of_files * sizeof(struct file));
    file_table[p].name = strdup(file_name);
    file_table[p].offset = offset;
    return p;
}

static char *str_tables = 0;
static int str_tables_len = 0;

static inline int add_string(char *str) {
    int offset;
    offset = str_tables_len;
    str_tables_len += strlen(str) + 1;
    DO_REALLOC(str_tables, str_tables_len * sizeof(char));
    strcpy(&str_tables[offset], str);

    return offset;
}

static void get_hypervisor_info(char *in) {
    char *ptr, *save_ptr = 0, *at_str, *file;
    int e, d, pos = 0;
    unsigned int at;
    int prtos_conf_file = 0;

    for (ptr = optarg, at_str = 0, e = 0; (file = strtok_r(ptr, ":", &save_ptr)); ptr = 0, e++) {
        d = (file[0] == '#') ? 1 : 0;
        if (!d) {
            at_str = strpbrk(file, "@");
            if (at_str) {
                *at_str = 0;
                at_str++;
                at = strtoul(at_str, 0, 16);
            } else
                at = 0;
            pos = add_file(file, at);
        } else {
            d = strtoul(&file[1], 0, 10);
            if ((d >= 0) && (d < num_of_files)) {
                pos = d;
            } else
                error_printf("There isn't any file at position %d", d);
        }

        if (!e) {
            hypervisor->id = -1;
            hypervisor->file = pos;
            hypervisor->num_of_custom_files = 0;
        } else {
            if ((hypervisor->num_of_custom_files + 1) > CONFIG_MAX_NO_CUSTOMFILES)
                error_printf("Only %d customisation files are permitted", CONFIG_MAX_NO_CUSTOMFILES);
            hypervisor->custom_file_table[hypervisor->num_of_custom_files] = pos;
            if (!hypervisor->num_of_custom_files) prtos_conf_file++;
            hypervisor->num_of_custom_files++;
        }
    }
    if (!prtos_conf_file) error_printf("prtos configuration (prtos_conf) file is missed");
}

static void get_partition_info(char *in) {
    char *ptr, *save_ptr = 0, *at_str, *file;
    struct prtos_exec_partition *partition;
    int e, d, id = -1, pos = 0;
    unsigned int at;

    num_of_partitions++;
    DO_REALLOC(partition_table, num_of_partitions * sizeof(struct prtos_exec_partition));
    partition = &partition_table[num_of_partitions - 1];

    for (ptr = optarg, at_str = 0, e = 0; (file = strtok_r(ptr, ":", &save_ptr)); ptr = 0, e++) {
        if (id < 0) {
            if (((id = strtoul(file, 0, 10)) < 0)) error_printf("Partition ID shall be a positive integer");

            partition->id = id;
            continue;
        }
        d = (file[0] == '#') ? 1 : 0;
        if (!d) {
            at_str = strpbrk(file, "@");
            if (at_str) {
                *at_str = 0;
                at_str++;
                at = strtoul(at_str, 0, 16);
            } else
                at = 0;
            pos = add_file(file, at);
        } else {
            d = strtoul(&file[1], 0, 10);
            if ((d >= 0) && (d < num_of_files))
                pos = d;
            else
                error_printf("There isn't any file at position %d", d);
        }

        if (e == 1) {
            partition->file = pos;
            partition->num_of_custom_files = 0;
        } else {
            if ((partition->num_of_custom_files + 1) > CONFIG_MAX_NO_CUSTOMFILES)
                error_printf("Only %d customisation files are permitted", CONFIG_MAX_NO_CUSTOMFILES);

            partition->custom_file_table[partition->num_of_custom_files] = pos;
            partition->num_of_custom_files++;
        }
    }
}

static void write_container_to_file(char *file) {
    int fd, pos = 0, e, i, size, file_size;
    struct digest_ctx digest_state;
    unsigned char *img;

    if ((fd = open(file, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP)) < 0) error_printf("File \"%s\" couldn't be created", file);

    prtos_exec_container_hdr.num_of_partitions = RWORD(prtos_exec_container_hdr.num_of_partitions);
    prtos_exec_container_hdr.num_of_files = RWORD(prtos_exec_container_hdr.num_of_files);
    prtos_exec_container_hdr.file_data_length = RWORD(prtos_exec_container_hdr.file_data_length);

    for (e = 0; e < num_of_partitions; e++) {
        partition_table[e].id = RWORD(partition_table[e].id);
        partition_table[e].file = RWORD(partition_table[e].file);
        for (i = 0; i < partition_table[e].num_of_custom_files; i++) {
            partition_table[e].custom_file_table[i] = RWORD(partition_table[e].custom_file_table[i]);
        }
        partition_table[e].num_of_custom_files = RWORD(partition_table[e].num_of_custom_files);
    }

    lseek(fd, sizeof(struct prtos_exec_container_hdr), SEEK_SET);
    pos = sizeof(struct prtos_exec_container_hdr);
    pos = ALIGNTO(pos, 8);
    lseek(fd, pos, SEEK_SET);
    prtos_exec_container_hdr.partition_table_offset = RWORD(pos);
    DO_WRITE(fd, partition_table, sizeof(struct prtos_exec_partition) * num_of_partitions);
    pos += sizeof(struct prtos_exec_partition) * num_of_partitions;

    pos = ALIGNTO(pos, 8);
    lseek(fd, pos, SEEK_SET);
    prtos_exec_container_hdr.string_table_offset = RWORD(pos);
    prtos_exec_container_hdr.str_len = RWORD(str_tables_len);
    DO_WRITE(fd, str_tables, str_tables_len);
    pos += str_tables_len;

    pos = ALIGNTO(pos, 8);
    prtos_exec_container_hdr.file_table_offset = pos;
    pos += sizeof(struct prtos_exec_file) * num_of_files;

    for (e = 0, size = 0; e < num_of_files; e++) {
        if (file_table[e].offset) {
            if (pos > file_table[e].offset) error_printf("Container couldn't be built with the specified offsets\n");
            pos = file_table[e].offset;
        }
        pos = ALIGNTO(pos, PAGE_SIZE);
        prtos_exec_file_table[e].offset = RWORD(pos);
        lseek(fd, pos, SEEK_SET);
        DO_WRITE(fd, &file_data[size], sizeof(prtos_u8_t) * prtos_exec_file_table[e].size);
        pos += prtos_exec_file_table[e].size;
        size += prtos_exec_file_table[e].size;
        prtos_exec_file_table[e].size = RWORD(prtos_exec_file_table[e].size);
        prtos_exec_file_table[e].name_offset = RWORD(prtos_exec_file_table[e].name_offset);
    }

    prtos_exec_container_hdr.file_data_offset = RWORD(pos);
    lseek(fd, prtos_exec_container_hdr.file_table_offset, SEEK_SET);
    prtos_exec_container_hdr.file_table_offset = RWORD(prtos_exec_container_hdr.file_table_offset);
    DO_WRITE(fd, prtos_exec_file_table, sizeof(struct prtos_exec_file) * num_of_files);
    for (e = 0; e < PRTOS_DIGEST_BYTES; e++) prtos_exec_container_hdr.digest[e] = 0;
    file_size = lseek(fd, 0, SEEK_END);
    if (file_size & 3) {  // Filling the container with padding
        prtos_u32_t padding = 0;
        DO_WRITE(fd, &padding, 4 - (file_size & 3));
        file_size = lseek(fd, 0, SEEK_END);
    }
    prtos_exec_container_hdr.flags = PRTOSEF_CONTAINER_DIGEST;
    prtos_exec_container_hdr.flags = RWORD(prtos_exec_container_hdr.flags);
    prtos_exec_container_hdr.file_size = RWORD(file_size);

    lseek(fd, 0, SEEK_SET);
    DO_WRITE(fd, &prtos_exec_container_hdr, sizeof(struct prtos_exec_container_hdr));

    lseek(fd, 0, SEEK_SET);
    img = malloc(file_size);

    if (read(fd, img, file_size) != file_size) error_printf("Unable to read the container file");

    digest_init(&digest_state);
    digest_update(&digest_state, img, file_size);
    digest_final(prtos_exec_container_hdr.digest, &digest_state);
    free(img);
    /*for (e=0; e<PRTOS_DIGEST_BYTES; e++)
        fprintf(stderr, "%02x", prtos_exec_container_hdr.digest[e]);
    fprintf(stderr, " %s\n", file);
    */
    lseek(fd, 0, SEEK_SET);
    DO_WRITE(fd, &prtos_exec_container_hdr, sizeof(struct prtos_exec_container_hdr));
    close(fd);
}

static void do_build(int argc, char **argv) {
    extern void do_check_to_build(int argc, char **argv);
    char *out_file = 0;
    int opt, hyp = 0, fd, e, ptr, r;
    char buffer[4096];

    do_check_to_build(argc, argv);
    memset(&prtos_exec_container_hdr, 0, sizeof(struct prtos_exec_container_hdr));
    prtos_exec_container_hdr.signature = RWORD(PRTOS_PACKAGE_SIGNATURE);
    prtos_exec_container_hdr.version = RWORD(PRTOS_SET_VERSION(PRTOS_PACK_VERSION, PRTOS_PACK_SUBVERSION, PRTOS_PACK_REVISION));
    num_of_partitions++;
    DO_REALLOC(partition_table, sizeof(struct prtos_exec_partition));
    hypervisor = &partition_table[0];
    while ((opt = getopt(argc, argv, "p:h:")) != -1) {
        switch (opt) {
            case 'p':
                get_partition_info(optarg);
                break;
            case 'h':
                if (!hyp)
                    hyp = 1;
                else
                    error_printf("prtos hypervisor has been alredy defined");

                get_hypervisor_info(optarg);
                break;
            default:
                fprintf(stderr, USAGE);
                exit(2);
        }
    }

    if (!hyp) error_printf("prtos hypervisor missed");

    if ((argc - optind) != 1) {
        fprintf(stderr, USAGE);
        exit(2);
    }

    out_file = argv[optind];

    DO_REALLOC(prtos_exec_file_table, sizeof(struct prtos_exec_file) * num_of_files);
    for (e = 0; e < num_of_files; e++) {
        char *file_name;
        if ((fd = open(file_table[e].name, O_RDONLY)) < 0) error_printf("File \"%s\" couldn't be opened", file_table[e].name);
        prtos_exec_file_table[e].size = lseek(fd, 0, SEEK_END);
        prtos_exec_file_table[e].offset = file_data_length;
        file_name = basename(strdup(file_table[e].name));
        prtos_exec_file_table[e].name_offset = add_string(file_name);
        ptr = file_data_length;
        file_data_length += prtos_exec_file_table[e].size;
        // file_data_length=ALIGNTO(file_data_length, 8);
        DO_REALLOC(file_data, file_data_length * sizeof(prtos_u8_t));
        lseek(fd, 0, SEEK_SET);
        while ((r = read(fd, buffer, 4096))) {
            memcpy(&file_data[ptr], buffer, r);
            ptr += 4096;
        }
        close(fd);
    }

    prtos_exec_container_hdr.num_of_partitions = num_of_partitions;
    prtos_exec_container_hdr.num_of_files = num_of_files;
    prtos_exec_container_hdr.file_data_length = file_data_length;
    write_container_to_file(out_file);
}

static void read_container_from_file(char *file) {
    prtos_u8_t digest[PRTOS_DIGEST_BYTES];
    struct digest_ctx digest_state;
    int fd, e, i, file_size;
    unsigned char *img;

    if ((fd = open(file, O_RDONLY)) < 0) error_printf("File \"%s\" couldn't be opened", file);

    file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    img = malloc(file_size);

    if (read(fd, img, file_size) != file_size) error_printf("Unable to read %s", file);

    for (e = 0; e < PRTOS_DIGEST_BYTES; e++) ((struct prtos_exec_container_hdr *)img)->digest[e] = 0;

    digest_init(&digest_state);
    digest_update(&digest_state, img, file_size);
    digest_final((prtos_u8_t *)digest, &digest_state);
    free(img);

    lseek(fd, 0, SEEK_SET);
    DO_READ(fd, &prtos_exec_container_hdr, sizeof(struct prtos_exec_container_hdr));
    prtos_exec_container_hdr.signature = RWORD(prtos_exec_container_hdr.signature);
    prtos_exec_container_hdr.version = RWORD(prtos_exec_container_hdr.version);
    prtos_exec_container_hdr.flags = RWORD(prtos_exec_container_hdr.flags);
    num_of_partitions = prtos_exec_container_hdr.num_of_partitions = RWORD(prtos_exec_container_hdr.num_of_partitions);
    prtos_exec_container_hdr.partition_table_offset = RWORD(prtos_exec_container_hdr.partition_table_offset);
    num_of_files = prtos_exec_container_hdr.num_of_files = RWORD(prtos_exec_container_hdr.num_of_files);
    prtos_exec_container_hdr.file_table_offset = RWORD(prtos_exec_container_hdr.file_table_offset);
    prtos_exec_container_hdr.file_data_offset = RWORD(prtos_exec_container_hdr.file_data_offset);
    file_data_length = prtos_exec_container_hdr.file_data_length = RWORD(prtos_exec_container_hdr.file_data_length);
    prtos_exec_container_hdr.file_size = RWORD(prtos_exec_container_hdr.file_size);
    prtos_exec_container_hdr.string_table_offset = RWORD(prtos_exec_container_hdr.string_table_offset);
    prtos_exec_container_hdr.str_len = RWORD(prtos_exec_container_hdr.str_len);
    str_tables_len = prtos_exec_container_hdr.str_len;

    if (prtos_exec_container_hdr.signature != PRTOS_PACKAGE_SIGNATURE) error_printf("\"%s\" is not a valid package", file);

    if (prtos_exec_container_hdr.flags & PRTOSEF_CONTAINER_DIGEST) {
        fprintf(stderr, "Digest: ");
        for (e = 0; e < PRTOS_DIGEST_BYTES; e++) fprintf(stderr, "%02x", digest[e]);
        fprintf(stderr, " %s ", file);
        for (e = 0; e < PRTOS_DIGEST_BYTES; e++)
            if (digest[e] != prtos_exec_container_hdr.digest[e]) {
                fprintf(stderr, "\n");
                error_printf("Incorrect digest: container corrupted\n");
            }
        fprintf(stderr, " Ok\n");
    }
    DO_REALLOC(file_img, prtos_exec_container_hdr.file_size);
    lseek(fd, 0, SEEK_SET);
    DO_READ(fd, file_img, prtos_exec_container_hdr.file_size);
    DO_REALLOC(file_data, file_data_length * sizeof(prtos_u8_t));
    DO_REALLOC(partition_table, num_of_partitions * sizeof(struct prtos_exec_partition));
    DO_REALLOC(prtos_exec_file_table, num_of_files * sizeof(struct prtos_exec_file));
    lseek(fd, prtos_exec_container_hdr.partition_table_offset, SEEK_SET);
    DO_READ(fd, partition_table, num_of_partitions * sizeof(struct prtos_exec_partition));
    lseek(fd, prtos_exec_container_hdr.file_table_offset, SEEK_SET);
    DO_READ(fd, prtos_exec_file_table, num_of_files * sizeof(struct prtos_exec_file));
    lseek(fd, prtos_exec_container_hdr.file_data_offset, SEEK_SET);
    read(fd, &prtos_exec_container_hdr, sizeof(struct prtos_exec_container_hdr));
    lseek(fd, prtos_exec_container_hdr.string_table_offset, SEEK_SET);
    DO_MALLOC(str_tables, str_tables_len);
    DO_READ(fd, str_tables, str_tables_len);
    // Process all structures' content
    for (e = 0; e < num_of_partitions; e++) {
        partition_table[e].id = RWORD(partition_table[e].id);
        partition_table[e].file = RWORD(partition_table[e].file);
        partition_table[e].num_of_custom_files = RWORD(partition_table[e].num_of_custom_files);
        for (i = 0; i < partition_table[e].num_of_custom_files; i++) partition_table[e].custom_file_table[i] = RWORD(partition_table[e].custom_file_table[i]);
    }
    for (e = 0; e < num_of_files; e++) {
        prtos_exec_file_table[e].offset = RWORD(prtos_exec_file_table[e].offset);
        prtos_exec_file_table[e].size = RWORD(prtos_exec_file_table[e].size);
        prtos_exec_file_table[e].name_offset = RWORD(prtos_exec_file_table[e].name_offset);
    }
}

#define _SP "    "

static void do_list(int argc, char **argv) {
    char *file = argv[argc - 1];
    prtos_s32_t e, i;
    if (argc != 2) {
        fprintf(stderr, USAGE);
        exit(0);
    }

    read_container_from_file(file);

    fprintf(stderr, "<Container file=\"%s\" version=\"%d.%d.%d\" flags=\"0x%x\">\n", file, PRTOS_GET_VERSION(prtos_exec_container_hdr.version),
            PRTOS_GET_SUBVERSION(prtos_exec_container_hdr.version), PRTOS_GET_REVISION(prtos_exec_container_hdr.version), prtos_exec_container_hdr.flags);
    for (e = 0; e < prtos_exec_container_hdr.num_of_partitions; e++) {
        if (!e)
            fprintf(stderr, _SP "<PRTOSHypervisor ");
        else
            fprintf(stderr, _SP "<Partition id=\"%d\" ", partition_table[e].id);

        fprintf(stderr, "file=\"%d\"", partition_table[e].file);
        fprintf(stderr, ">\n");
        for (i = 0; i < partition_table[e].num_of_custom_files; i++) {
            fprintf(stderr, _SP _SP "<custom_file ");
            fprintf(stderr, "file=\"%d\" ", partition_table[e].custom_file_table[i]);
            fprintf(stderr, "/>\n");
        }
        if (!e)
            fprintf(stderr, _SP "</PRTOSHypervisor>\n");
        else
            fprintf(stderr, _SP "</Partition>\n");
    }
    fprintf(stderr, _SP "<FileTable>\n");
    for (e = 0; e < prtos_exec_container_hdr.num_of_files; e++) {
        fprintf(stderr, _SP _SP "<File entry=\"%d\" name=\"%s\" ", e, &str_tables[prtos_exec_file_table[e].name_offset]);
        fprintf(stderr, "offset=\"0x%" PRINT_PREF "x\" ", prtos_exec_file_table[e].offset);
        fprintf(stderr, "size=\"%" PRINT_PREF "d\" ", prtos_exec_file_table[e].size);
        fprintf(stderr, "/>\n");
    }
    fprintf(stderr, _SP "</FileTable>\n");
    fprintf(stderr, "</Container>\n");
}

extern void do_check(int argc, char **argv);

int main(int argc, char **argv) {
    char **_argv;
    int e, i;
    if (argc < 2) {
        fprintf(stderr, USAGE);
        exit(2);
    }

    _argv = malloc(sizeof(char *) * (argc - 1));
    for (e = 0, i = 0; e < argc; e++) {
        if (e == 1) continue;
        _argv[i] = strdup(argv[e]);
        i++;
    }

    if (!strcasecmp(argv[1], "list")) {
        do_list(argc - 1, _argv);
        exit(0);
    }

    if (!strcasecmp(argv[1], "build")) {
        do_build(argc - 1, _argv);
        exit(0);
    }

    if (!strcasecmp(argv[1], "check")) {
        do_check(argc - 1, _argv);
        exit(0);
    }

    fprintf(stderr, USAGE);
    return 0;
}
