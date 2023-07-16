/*
 * FILE: prtoscbuild.c
 *
 * Compile the c code
 *
 * www.prtos.org
 */

#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <endianess.h>
#include <prtos_inc/digest.h>
#include <prtos_inc/prtosconf.h>

#include "common.h"

#define CONFIG_FILE "prtos_config"

#define CONFIG_PATH "%s/" CONFIG_FILE

#define CFLAGS "-O2 -Wall -I${PRTOS_PATH}/user/libprtos/include -D\"__PRTOS_INCFLD(_fld)=<prtos_inc/_fld>\" -I${PRTOS_PATH}/include -nostdlib -nostdinc"

#define MAKEFILE                                                                                                                                             \
    "include %s\nrun:a.c.prtos_conf\n\t${TARGET_CC} ${TARGET_CFLAGS_ARCH} -x c %s --include prtos_inc/config.h --include prtos_inc/arch/arch_types.h %s -o " \
    "%s "                                                                                                                                                    \
    "-Wl,--entry=0x0,-T%s\n.PHONY: run\n"

static char lds_content[] = "OUTPUT_FORMAT(\"binary\")\n"
                            "SECTIONS\n"
                            "{\n"
                            "         . = 0x0;\n"
                            "         .data ALIGN (8) : {\n"
                            "      	     *(.rodata.hdr)\n"
                            "    	     *(.rodata)\n"
                            "    	     *(.data)\n"
                            "                _mem_obj_table = .;\n"
                            "                *(.data.memobj)\n"
                            "                LONG(0);\n"
                            "        }\n"
                            "\n"
                            "        _dataSize = .;\n"
                            "\n"
                            "        .bss ALIGN (8) : {\n"
                            "                *(.bss)\n"
                            "    	     *(.bss.mempool)\n"
                            "    	}\n"
                            "\n"
                            "    	_prtoscSize = .;\n"
                            "\n"
                            " 	/DISCARD/ : {\n"
                            "	   	*(.text)\n"
                            "    	*(.note)\n"
                            "	    	*(.comment*)\n"
                            "	}\n"
                            "}\n";

static void write_ldsfile(char *lds_file) {
    int fd = mkstemp(lds_file);

    if ((fd < 0) || (write(fd, lds_content, strlen(lds_content)) != strlen(lds_content))) error_printf("unable to create the lds file\n");
    close(fd);
}

static void write_makefile(char *file_name, char *content) {
    int fd = mkstemp(file_name);

    if ((fd < 0) || (write(fd, content, strlen(content)) != strlen(content))) error_printf("unable to create the makefile file\n");
    close(fd);
}

#define DEV_INC_PATH "/user/libprtos/include"
#define INST_INC_PATH "/include"

void exec_xml_conf_build(char *path, char *in, char *out) {
    char *prtos_path, *config_path, *make_file, *build_cmd;
    char lds_file[] = "ldsXXXXXX";
    char file_name[] = "makefileXXXXXX";

    if (!(prtos_path = getenv("PRTOS_PATH"))) error_printf("The PRTOS_PATH enviroment variable must be set\n");

    DO_MALLOC(config_path, strlen(CONFIG_PATH) + strlen(prtos_path) + 1);
    sprintf(config_path, CONFIG_PATH, prtos_path);
    write_ldsfile(lds_file);

    DO_MALLOC(make_file, strlen(MAKEFILE) + strlen(config_path) + strlen(CFLAGS) + strlen(in) + strlen(out) + strlen(lds_file) + 1);
    sprintf(make_file, MAKEFILE, config_path, CFLAGS, in, out, lds_file);

    write_makefile(file_name, make_file);
    // fprintf(stderr, "%s", make_file);

    int str_len = strlen("make -f ") + strlen(file_name) + 1;
    printf("str_len = %d\n", str_len);

    DO_MALLOC(build_cmd, strlen(CONFIG_PATH) + strlen(prtos_path) + 1);
    sprintf(build_cmd, "make -f %s\n", file_name);
    // fprintf(stderr, "%s", build_cmd);
    if (system(build_cmd)) fprintf(stderr, "Error building prtos_conf file\n");

    unlink(file_name);
    unlink(lds_file);
}

void calc_digest(char *out) {
    prtos_u8_t digest[PRTOS_DIGEST_BYTES];
    struct digest_ctx digestState;
    prtos_u32_t signature;
    prtos_u_size_t dataSize;
    prtos_u8_t *buffer;
    int fd;

    memset(digest, 0, PRTOS_DIGEST_BYTES);
    if ((fd = open(out, O_RDWR)) < 0) error_printf("File %s cannot be opened\n", out);

    fsync(fd);
    DO_READ(fd, &signature, sizeof(prtos_u32_t));
    if (RWORD(signature) != PRTOSC_SIGNATURE) error_printf("File signature unknown (%x)\n", signature);

    lseek(fd, offsetof(struct prtos_conf, dataSize), SEEK_SET);

    DO_READ(fd, &dataSize, sizeof(prtos_u_size_t));
    dataSize = RWORD(dataSize);
    DO_MALLOC(buffer, dataSize);
    lseek(fd, 0, SEEK_SET);
    DO_READ(fd, buffer, dataSize);
    digest_init(&digestState);
    digest_update(&digestState, buffer, offsetof(struct prtos_conf, digest));
    digest_update(&digestState, (prtos_u8_t *)digest, PRTOS_DIGEST_BYTES);
    digest_update(&digestState, &buffer[offsetof(struct prtos_conf, dataSize)], dataSize - offsetof(struct prtos_conf, dataSize));
    digest_final(digest, &digestState);
    free(buffer);

    lseek(fd, offsetof(struct prtos_conf, digest), SEEK_SET);
    DO_WRITE(fd, digest, PRTOS_DIGEST_BYTES);
    close(fd);
}
