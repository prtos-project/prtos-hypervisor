/*
 * FILE: rsvmem.c
 *
 * Reserve memory
 *
 * www.prtos.org
 */

#include <stdlib.h>
#include <string.h>
#include <common.h>

static struct block {
    unsigned int size;
    int align;
    char *comment;
} *block_table = 0;

static int num_of_blocks = 0;

void rsv_block(unsigned int size, int align, char *comment) {
    if (!size || !align) return;
    num_of_blocks++;
    DO_REALLOC(block_table, num_of_blocks * sizeof(struct block));
    block_table[num_of_blocks - 1].size = size;
    block_table[num_of_blocks - 1].align = align;
    block_table[num_of_blocks - 1].comment = strdup(comment);
}

#define MEM_BLOCK(size, align, comment)                             \
    do {                                                            \
        if (size) {                                                 \
            fprintf(out_file,                                       \
                    "\n__asm__ (/* %s */ \\\n"                      \
                    "         \".section .data.memobj\\n\\t\" \\\n" \
                    "         \".long 1f\\n\\t\" \\\n"              \
                    "         \".long %d\\n\\t\" \\\n"              \
                    "         \".long %d\\n\\t\" \\\n"              \
                    "         \".section .bss.mempool\\n\\t\" \\\n" \
                    "         \".align %d\\n\\t\" \\\n"             \
                    "         \"1:.zero %d\\n\\t\" \\\n"            \
                    "         \".previous\\n\\t\");\n",             \
                    comment, align, size, align, size);             \
        }                                                           \
    } while (0)

static int Cmp(struct block *b1, struct block *b2) {
    if (b1->align > b2->align) return 1;
    if (b1->align < b2->align) return -1;
    return 0;
}

void print_blocks(FILE *out_file) {
    int e;
    qsort(block_table, num_of_blocks, sizeof(struct block), (int (*)(const void *, const void *))Cmp);
    for (e = 0; e < num_of_blocks; e++) MEM_BLOCK(block_table[e].size, block_table[e].align, block_table[e].comment);
}
