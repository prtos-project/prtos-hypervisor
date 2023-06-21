/*
 * FILE: xmlcheck.c
 *
 * Validates a partition xef against a xml xef file
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
#include <xef.h>
#include <prtos_inc/prtosconf.h>
#include <prtos_inc/arch/arch_types.h>
#include <prtos_inc/prtosef.h>
#include <prtos_inc/compress.h>
#include <prtos_inc/digest.h>

#include <endianess.h>
#include <prtospack.h>

struct file_to_process {
    char *name;
    struct xef_file xef;
};

static prtos_u8_t *load_file(char *name) {
    prtos_s32_t file_size;
    prtos_u8_t *buffer;
    int fd;
    if ((fd = open(name, O_RDONLY)) < 0) error_printf("File \"%s\" couldn't be opened", name);

    file_size = lseek(fd, 0, SEEK_END);
    DO_MALLOC(buffer, file_size);
    lseek(fd, 0, SEEK_SET);
    DO_READ(fd, buffer, file_size);
    return buffer;
}

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

static struct prtos_conf *parse_prtos_conf(struct file_to_process *prtos_conf_file) {
    struct prtos_conf *prtos_conf_table = 0;
    prtos_u8_t *uimg;
    if ((RWORD(prtos_conf_file->xef.hdr->flags) & XEF_TYPE_MASK) != XEF_TYPE_CUSTOMFILE)
        error_printf("File \"%s\": expecting a XML customization file", prtos_conf_file->name);

    if (RWORD(prtos_conf_file->xef.hdr->flags) & XEF_COMPRESSED) {
        prtos_s32_t ptr, e;
        DO_MALLOC(uimg, RWORD(prtos_conf_file->xef.hdr->image_length));
        for (ptr = 0, e = 0; e < RWORD(prtos_conf_file->xef.hdr->num_of_segments); e++, ptr += RWORD(prtos_conf_file->xef.segment_table[e].file_size)) {
            prtos_u8_t *rPtr, *wPtr;
            rPtr = (prtos_u8_t *)&prtos_conf_file->xef.image[RWORD(prtos_conf_file->xef.segment_table[e].offset)];
            wPtr = (prtos_u8_t *)&uimg[ptr];
            uncompress(RWORD(prtos_conf_file->xef.segment_table[e].deflated_file_size), RWORD(prtos_conf_file->xef.segment_table[e].file_size), u_read, &rPtr,
                       u_write, &wPtr);
        }
        prtos_conf_table = (struct prtos_conf *)uimg;
    } else {
        prtos_conf_table = (struct prtos_conf *)prtos_conf_file->xef.image;
    }

    if (!prtos_conf_table || prtos_conf_table->signature != RWORD(PRTOSC_SIGNATURE))
        error_printf("File \"%s\": invalid PRTOSC signature (0x%x)", prtos_conf_file->name, prtos_conf_table->signature);

    return prtos_conf_table;
}

static void check_partition(int part_id, struct file_to_process *part, struct file_to_process *custom_table, int num_of_custom,
                            struct prtos_conf *prtos_conf_table) {
    struct prtos_conf_part *prtos_conf_part_table;
    struct prtos_conf_memory_area *prtos_conf_mem_area;
    prtos_address_t a, b, c, d;
    int num_of_mem_areas;
    int e, s, hyp = 0, i;
    prtos_conf_mem_area = (struct prtos_conf_memory_area *)&((char *)prtos_conf_table)[RWORD(prtos_conf_table->physical_memory_areas_offset)];
    prtos_conf_part_table = (struct prtos_conf_part *)&((char *)prtos_conf_table)[RWORD(prtos_conf_table->part_table_offset)];

    if (((RWORD(part->xef.hdr->flags) & XEF_TYPE_MASK) != XEF_TYPE_PARTITION) && ((RWORD(part->xef.hdr->flags) & XEF_TYPE_MASK) != XEF_TYPE_HYPERVISOR))
        error_printf("File \"%s\": prtos or a partition expected", part->name);

    for (e = 0; e < num_of_custom; e++)
        if ((RWORD(custom_table[e].xef.hdr->flags) & XEF_TYPE_MASK) != XEF_TYPE_CUSTOMFILE)
            error_printf("File \"%s\": customization file expected ", custom_table[e].name);

    if (num_of_custom != RWORD(part->xef.hdr->num_of_custom_files))
        // error_printf
        fprintf(stderr, "File \"%s\": %d customization files expected, %d provided\n", part->name, RWORD(part->xef.hdr->num_of_custom_files), num_of_custom);

    if ((RWORD(part->xef.hdr->flags) & XEF_TYPE_MASK) == XEF_TYPE_HYPERVISOR) {
        prtos_conf_mem_area = &prtos_conf_mem_area[RWORD(prtos_conf_table->hpv.physical_memory_areas_offset)];
        num_of_mem_areas = RWORD(prtos_conf_table->hpv.num_of_physical_memory_areas);
        hyp = 1;
    } else {
        if ((part_id < 0) || (part_id >= RWORD(prtos_conf_table->num_of_partitions))) error_printf("PartitionId %d: not valid", part_id);
        prtos_conf_mem_area = &prtos_conf_mem_area[RWORD(prtos_conf_part_table[part_id].physical_memory_areas_offset)];
        num_of_mem_areas = RWORD(prtos_conf_part_table[part_id].num_of_physical_memory_areas);
    }

    for (s = 0; s < RWORD(part->xef.hdr->num_of_segments); s++) {
        c = RWORD(part->xef.segment_table[s].phys_addr);
        d = c + RWORD(part->xef.segment_table[s].file_size);
        for (e = 0; e < num_of_mem_areas; e++) {
            a = RWORD(prtos_conf_mem_area[e].start_addr);
            b = a + RWORD(prtos_conf_mem_area[e].size);
            if ((c >= a) && (d <= b)) break;
        }
        if (e >= num_of_mem_areas) error_printf("Partition \"%s\" (%d): segment %d [0x%x- 0x%x] does not fit (PRTOSC)", part->name, part_id, s, c, d);
    }

    for (e = 0; e < num_of_custom; e++) {
        if (RWORD(part->xef.custom_file_table[e].size)) {
            if (RWORD(custom_table[e].xef.hdr->image_length) > RWORD(part->xef.custom_file_table[e].size))
                error_printf("Customization file \"%s\": too big (%d bytes), partition \"%s\" only reserves %d bytes", custom_table[e].name,
                             RWORD(custom_table[e].xef.hdr->image_length), part->name, RWORD(part->xef.custom_file_table[e].size));
        }
        c = RWORD(part->xef.custom_file_table[e].sAddr);
        if (hyp && (e == 0)) {  // XML
            d = c + RWORD(prtos_conf_table->size);
        } else
            d = c + RWORD(custom_table[e].xef.hdr->image_length);

        for (i = 0; i < num_of_mem_areas; i++) {
            if (hyp) {
                a = RWORD(prtos_conf_mem_area[i].start_addr);
                b = a + RWORD(prtos_conf_mem_area[i].size);
            } else {
                a = RWORD(prtos_conf_mem_area[i].mapped_at);
                b = a + RWORD(prtos_conf_mem_area[i].size);
            }
            if ((c >= a) && (d <= b)) break;
        }

        if (i >= num_of_mem_areas) error_printf("Customization file \"%s\" (%d): [0x%x- 0x%x] does not fit (PRTOSC)", custom_table[e].name, e, c, d);
    }
}

static void get_partition_info(char *in, int *partition_id, struct file_to_process *part, struct file_to_process **custom_table, int *num_of_custom_files) {
    char *ptr, *savePtr = 0, *file;
    int e, id = -1;

    for (ptr = in, e = 0; (file = strtok_r(ptr, ":", &savePtr)); ptr = 0, e++) {
        if (id < 0) {
            if (((id = strtoul(file, 0, 10)) < 0)) error_printf("Partition ID shall be a positive integer");

            *partition_id = id;
            continue;
        }

        if (e == 1) {
            part->name = strdup(file);
            *num_of_custom_files = 0;
        } else {
            if (((*num_of_custom_files) + 1) > CONFIG_MAX_NO_CUSTOMFILES) error_printf("Only %d customisation files are permitted", CONFIG_MAX_NO_CUSTOMFILES);
            (*num_of_custom_files)++;
            DO_REALLOC((*custom_table), (*num_of_custom_files) * sizeof(struct file_to_process));
            (*custom_table)[(*num_of_custom_files) - 1].name = strdup(file);
        }
    }
}

static void get_hypervisor_info(char *in, struct file_to_process *part, struct file_to_process **custom_table, int *num_of_custom_files) {
    char *ptr, *savePtr = 0, *file;
    int e;

    for (ptr = in, e = 0; (file = strtok_r(ptr, ":", &savePtr)); ptr = 0, e++) {
        if (e == 0) {
            part->name = strdup(file);
            *num_of_custom_files = 0;
        } else {
            if (((*num_of_custom_files) + 1) > CONFIG_MAX_NO_CUSTOMFILES) error_printf("Only %d customisation files are permitted", CONFIG_MAX_NO_CUSTOMFILES);
            (*num_of_custom_files)++;
            DO_REALLOC((*custom_table), (*num_of_custom_files) * sizeof(struct file_to_process));
            (*custom_table)[(*num_of_custom_files) - 1].name = strdup(file);
        }
    }
}

void do_check(int argc, char **argv) {
    struct file_to_process prtos_conf, part, *custom_table = 0;
    int ret, num_of_custom = 0, e, i, part_id = 0;
    struct prtos_conf *prtos_conf_table;
    if (argc < 4) {
        fprintf(stderr, USAGE);
        exit(2);
    }

    prtos_conf.name = strdup(argv[1]);
    if ((ret = parse_xef_file(load_file(prtos_conf.name), &prtos_conf.xef)) != XEF_OK) error_printf("Error loading XEF file \"%s\": %d", prtos_conf.name, ret);
    prtos_conf_table = parse_prtos_conf(&prtos_conf);

    for (e = 2; e < argc; e++) {
        if (!strcmp(argv[e], "-h")) {
            get_hypervisor_info(argv[++e], &part, &custom_table, &num_of_custom);
        } else if (!strcmp(argv[e], "-p")) {
            get_partition_info(argv[++e], &part_id, &part, &custom_table, &num_of_custom);
        } else {
            fprintf(stderr, "Ignoring unexpected argument (%s)\n", argv[e]);
            continue;
        }
        if ((ret = parse_xef_file(load_file(part.name), &part.xef)) != XEF_OK) error_printf("Error loading XEF file \"%s\": %d", part.name, ret);
        if (num_of_custom) {
            for (i = 0; i < num_of_custom; i++) {
                if ((ret = parse_xef_file(load_file(custom_table[i].name), &custom_table[i].xef)) != XEF_OK)
                    error_printf("Error loading XEF file \"%s\": %d", custom_table[i].name, ret);
            }
        }
        fprintf(stderr, "> %s ... ", part.name);
        check_partition(part_id, &part, custom_table, num_of_custom, prtos_conf_table);
        fprintf(stderr, "ok\n");
    }
}

void do_check_to_build(int argc, char **argv) {
    struct file_to_process prtos_conf, part, *custom_table = 0;
    int ret, num_of_custom = 0, e, i, part_id = 0;
    struct prtos_conf *prtos_conf_table = 0;
    char **_argv;

    DO_MALLOC(_argv, argc * sizeof(char *));

    for (e = 0; e < argc; e++) _argv[e] = strdup(argv[e]);

    for (e = 0; e < argc; e++) {
        if (!strcmp(_argv[e], "-h")) {
            get_hypervisor_info(_argv[++e], &part, &custom_table, &num_of_custom);
            if (num_of_custom < 1) error_printf("Hypervisor requires at least one custom file");
            prtos_conf.name = strdup(custom_table[0].name);
        } else if (!strcmp(_argv[e], "-p")) {
            get_partition_info(_argv[++e], &part_id, &part, &custom_table, &num_of_custom);
        } else {
            continue;
        }
        if ((ret = parse_xef_file(load_file(part.name), &part.xef)) != XEF_OK) error_printf("Error loading XEF file \"%s\": %d", part.name, ret);
        if (num_of_custom) {
            for (i = 0; i < num_of_custom; i++) {
                if ((ret = parse_xef_file(load_file(custom_table[i].name), &custom_table[i].xef)) != XEF_OK)
                    error_printf("Error loading XEF file \"%s\": %d", custom_table[i].name, ret);
            }
        }
        if ((ret = parse_xef_file(load_file(prtos_conf.name), &prtos_conf.xef)) != XEF_OK)
            error_printf("Error loading XEF file \"%s\": %d", prtos_conf.name, ret);
        prtos_conf_table = parse_prtos_conf(&prtos_conf);
        fprintf(stderr, "> %s ... ", part.name);
        check_partition(part_id, &part, custom_table, num_of_custom, prtos_conf_table);
        fprintf(stderr, "ok\n");
    }
    for (e = 0; e < argc; e++) free(_argv[e]);
    free(_argv);
}
