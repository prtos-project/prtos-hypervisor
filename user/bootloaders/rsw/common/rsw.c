/*
 * FILE: rsw.c
 *
 * A boot rsw
 *
 * www.prtos.org
 */

#include <prtos.h>
#include <container.h>
#include <xef.h>
#include <prtos_inc/arch/arch_types.h>
#include <prtos_inc/prtosconf.h>
#include <prtos_inc/compress.h>
#include <prtos_inc/digest.h>
#include <rsw_stdc.h>

void halt_system(void) {
    extern void _halt_system(void);
    xprintf("[RSW] System Halted.\n");
    _halt_system();
}

static int prots_conf_vaddr_to_paddr(struct prtos_conf_memory_area *memory_areas, prtos_s32_t num_of_areas, prtos_address_t v_addr, prtos_address_t *p_addr) {
    prtos_s32_t e;
    for (e = 0; e < num_of_areas; e++) {
        if ((memory_areas[e].mapped_at <= v_addr) && (((memory_areas[e].mapped_at + memory_areas[e].size) - 1) >= v_addr)) {
            *p_addr = v_addr - memory_areas[e].mapped_at + memory_areas[e].start_addr;
            return 0;
        }
    }
    return -1;
}

extern prtos_address_t hpv_entry_point[];

void rsw_main(void) {
    extern prtos_u8_t *prtos_xef_container_ptr;
    extern void start(void);
    struct xef_container_file container;
    struct prtos_conf_boot_part *prtos_conf_boot_part_table;
    struct xef_file xef_file, xef_custom_file;
    struct prtos_hdr *prtos_hdr;
    struct prtos_conf *prtos_conf;
    struct prtos_conf_part *prtos_conf_parts;
    struct prtos_conf_memory_area *prtos_conf_mem_area;
    prtos_s32_t ret, min, e, i;

    init_output();
    xprintf("[RSW] Start Resident Software\n");
    // Parse container header
    if ((ret = parse_xef_container(prtos_xef_container_ptr, &container)) != CONTAINER_OK) {
        xprintf("[RSW] Error %d when parsing container file\n", ret);
        halt_system();
    }

    // Parse PRTOS header(.i.e: xefile) and check PRTOS file's digestion correctness
    if ((ret = parse_xef_file((prtos_u8_t *)(container.file_table[container.part_table[0].file].offset + prtos_xef_container_ptr), &xef_file)) != XEF_OK) {
        xprintf("[RSW] Error %d when parsing XEF file\n", ret);
        halt_system();
    }

    // Parse PRTOS XML configuration file header(.i.e: xef_custom_file) and check the configuation file's digestion correctness
    if ((ret = parse_xef_file((prtos_u8_t *)(container.file_table[container.part_table[0].custom_file_table[0]].offset + prtos_xef_container_ptr),
                              &xef_custom_file)) != XEF_OK) {
        xprintf("[RSW] Error %d when parsing XEF file\n", ret);
        halt_system();
    }

    // Load PRTOS XML configuation file to the specified memory address
    prtos_conf = load_xef_custom_file(&xef_custom_file, &xef_file.custom_file_table[0]);

    if (prtos_conf->signature != PRTOSC_SIGNATURE) {
        xprintf("[RSW] PRTOSC signature not found\n");
        halt_system();
    }

    // Load PRTOS Core file to the specified memory address
    prtos_hdr = load_xef_file(&xef_file, 0, 0, 0);
    hpv_entry_point[0] = xef_file.hdr->entry_point;
    if ((prtos_hdr->start_signature != PRTOS_EXEC_HYP_MAGIC) || (prtos_hdr->end_signature != PRTOS_EXEC_HYP_MAGIC)) {
        xprintf("[RSW] prtos signature not found\n");
        halt_system();
    }

    // Load additional custom files to the specifiled memory address
    min = (container.part_table[0].num_of_custom_files > prtos_hdr->num_of_custom_files) ? prtos_hdr->num_of_custom_files
                                                                                         : container.part_table[0].num_of_custom_files;

    for (e = 1; e < min; e++) {
        if ((ret = parse_xef_file((prtos_u8_t *)(container.file_table[container.part_table[0].custom_file_table[e]].offset + prtos_xef_container_ptr),
                                  &xef_custom_file)) != XEF_OK) {
            xprintf("[RSW] Error %d when parsing XEF file\n", ret);
            halt_system();
        }
        load_xef_custom_file(&xef_custom_file, &xef_file.custom_file_table[e]);
    }

    prtos_conf_boot_part_table = (struct prtos_conf_boot_part *)((prtos_address_t)prtos_conf + prtos_conf->boot_part_table_offset);
    prtos_conf_parts = (struct prtos_conf_part *)((prtos_address_t)prtos_conf + prtos_conf->part_table_offset);
    prtos_conf_mem_area = (struct prtos_conf_memory_area *)((prtos_address_t)prtos_conf + prtos_conf->physical_memory_areas_offset);
    // Loading partitions
    for (e = 1; e < container.hdr->num_of_partitions; e++) {
        struct prtos_image_hdr *part_hdr;
        if ((ret = parse_xef_file((prtos_u8_t *)(container.file_table[container.part_table[e].file].offset + prtos_xef_container_ptr), &xef_file)) != XEF_OK) {
            xprintf("[RSW] Error %d when parsing XEF file\n", ret);
            halt_system();
        }
        part_hdr = load_xef_file(&xef_file, (int (*)(void *, prtos_s32_t, prtos_address_t, prtos_address_t *))prots_conf_vaddr_to_paddr,
                                 &prtos_conf_mem_area[prtos_conf_parts[container.part_table[e].id].physical_memory_areas_offset],
                                 prtos_conf_parts[container.part_table[e].id].num_of_physical_memory_areas);
        if ((part_hdr->start_signature != PRTOS_EXEC_PARTITION_MAGIC) && (part_hdr->end_signature != PRTOS_EXEC_PARTITION_MAGIC)) {
            xprintf("[RSW] Partition signature not found (0x%x)\n", part_hdr->start_signature);
            halt_system();
        }
        prtos_conf_boot_part_table[container.part_table[e].id].flags |= PRTOS_PART_BOOT;
        prtos_conf_boot_part_table[container.part_table[e].id].hdr_phys_addr = (prtos_address_t)part_hdr;
        prtos_conf_boot_part_table[container.part_table[e].id].entry_point = xef_file.hdr->entry_point;
        prtos_conf_boot_part_table[container.part_table[e].id].image_start = (prtos_address_t)xef_file.hdr;
        prtos_conf_boot_part_table[container.part_table[e].id].img_size = xef_file.hdr->file_size;
        // Loading additional custom files
        min = (container.part_table[e].num_of_custom_files > part_hdr->num_of_custom_files) ? part_hdr->num_of_custom_files
                                                                                            : container.part_table[e].num_of_custom_files;

        if (min > CONFIG_MAX_NO_CUSTOMFILES) {
            xprintf("[RSW] Error when parsing XEF custom files. num_of_custom_files > CONFIG_MAX_NO_CUSTOMFILES\n");
            halt_system();
        }

        prtos_conf_boot_part_table[container.part_table[e].id].num_of_custom_files = min;

        for (i = 0; i < min; i++) {
            if ((ret = parse_xef_file((prtos_u8_t *)(container.file_table[container.part_table[e].custom_file_table[i]].offset + prtos_xef_container_ptr),
                                      &xef_custom_file)) != XEF_OK) {
                xprintf("[RSW] Error %d when parsing XEF file\n", ret);
                halt_system();
            }
            prtos_conf_boot_part_table[container.part_table[e].id].custom_file_table[i].sAddr = (prtos_address_t)xef_custom_file.hdr;
            prtos_conf_boot_part_table[container.part_table[e].id].custom_file_table[i].size = xef_custom_file.hdr->image_length;
            load_xef_custom_file(&xef_custom_file, &xef_file.custom_file_table[i]);
        }
    }

    xprintf("[RSW] Starting prtos at 0x%x\n", hpv_entry_point[0]);
    ((void (*)(void))ADDR2PTR(hpv_entry_point[0]))();
}
