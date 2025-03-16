/*
 * FILE: rsw.c
 *
 * A boot rsw
 *
 * www.prtos.org
 */

#include <prtos.h>
#include <container.h>
#include <pef.h>
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
    extern prtos_u8_t *prtos_pef_container_ptr;
    extern void start(void);
    struct pef_container_file container;
    struct prtos_conf_boot_part *prtos_conf_boot_partition_table;
    struct pef_file pef_file, pef_custom_file;
    struct prtos_hdr *prtos_hdr;
    struct prtos_conf *prtos_conf;
    struct prtos_conf_part *prtos_conf_parts;
    struct prtos_conf_memory_area *prtos_conf_mem_area;
    prtos_s32_t ret, min, e, i;

    init_output();
    xprintf("[RSW] Start Resident Software\n");

    // Parse container header
    if ((ret = parse_pef_container(prtos_pef_container_ptr, &container)) != CONTAINER_OK) {
        xprintf("[RSW] Error %d when parsing container file\n", ret);
        halt_system();
    }

    // Parse PRTOS header(.i.e: pefile) and check PRTOS file's digestion correctness
    if ((ret = parse_pef_file((prtos_u8_t *)(container.file_table[container.partition_table[0].file].offset + prtos_pef_container_ptr), &pef_file)) != PEF_OK) {
        xprintf("[RSW] Error %d when parsing PEF file\n", ret);
        halt_system();
    }

    // Parse PRTOS XML configuration file header(.i.e: pef_custom_file) and check the configuation file's digestion correctness
    if ((ret = parse_pef_file((prtos_u8_t *)(container.file_table[container.partition_table[0].custom_file_table[0]].offset + prtos_pef_container_ptr),
                              &pef_custom_file)) != PEF_OK) {
        xprintf("[RSW] Error %d when parsing PEF file\n", ret);
        halt_system();
    }

    // Load PRTOS XML configuation file to the specified memory address
    prtos_conf = load_pef_custom_file(&pef_custom_file, &pef_file.custom_file_table[0]);

    if (prtos_conf->signature != PRTOSC_SIGNATURE) {
        xprintf("[RSW] PRTOS Config File signature not found\n");
        halt_system();
    }

    // Load PRTOS Core file to the specified memory address
#if defined(CONFIG_AARCH64)
    prtos_address_t OFFSET = -CONFIG_PRTOS_OFFSET + CONFIG_PRTOS_LOAD_ADDR;
    prtos_hdr = load_pef_file(&pef_file, 0, 0, 0) + OFFSET;
    hpv_entry_point[0] = pef_file.hdr->entry_point + OFFSET;
#else
    prtos_hdr = load_pef_file(&pef_file, 0, 0, 0);
    hpv_entry_point[0] = pef_file.hdr->entry_point;
#endif
    if ((prtos_hdr->start_signature != PRTOS_EXEC_HYP_MAGIC) || (prtos_hdr->end_signature != PRTOS_EXEC_HYP_MAGIC)) {
        xprintf("[RSW] prtos signature not found\n");
        halt_system();
    }

    // Load additional custom files to the specifiled memory address
    min = (container.partition_table[0].num_of_custom_files > prtos_hdr->num_of_custom_files) ? prtos_hdr->num_of_custom_files
                                                                                              : container.partition_table[0].num_of_custom_files;
    for (e = 1; e < min; e++) {
        if ((ret = parse_pef_file((prtos_u8_t *)(container.file_table[container.partition_table[0].custom_file_table[e]].offset + prtos_pef_container_ptr),
                                  &pef_custom_file)) != PEF_OK) {
            xprintf("[RSW] Error %d when parsing PEF file\n", ret);
            halt_system();
        }
        load_pef_custom_file(&pef_custom_file, &pef_file.custom_file_table[e]);
    }

    prtos_conf_boot_partition_table = (struct prtos_conf_boot_part *)((prtos_address_t)prtos_conf + prtos_conf->boot_partition_table_offset);
    prtos_conf_parts = (struct prtos_conf_part *)((prtos_address_t)prtos_conf + prtos_conf->partition_table_offset);
    prtos_conf_mem_area = (struct prtos_conf_memory_area *)((prtos_address_t)prtos_conf + prtos_conf->physical_memory_areas_offset);
    // Loading partitions
    for (e = 1; e < container.hdr->num_of_partitions; e++) {
        struct prtos_image_hdr *part_hdr;
        if ((ret = parse_pef_file((prtos_u8_t *)(container.file_table[container.partition_table[e].file].offset + prtos_pef_container_ptr), &pef_file)) !=
            PEF_OK) {
            xprintf("[RSW] Error %d when parsing PEF file\n", ret);
            halt_system();
        }
        part_hdr = load_pef_file(&pef_file, (int (*)(void *, prtos_s32_t, prtos_address_t, prtos_address_t *))prots_conf_vaddr_to_paddr,
                                 &prtos_conf_mem_area[prtos_conf_parts[container.partition_table[e].id].physical_memory_areas_offset],
                                 prtos_conf_parts[container.partition_table[e].id].num_of_physical_memory_areas);
        if ((part_hdr->start_signature != PRTOS_EXEC_PARTITION_MAGIC) && (part_hdr->end_signature != PRTOS_EXEC_PARTITION_MAGIC)) {
            xprintf("[RSW] Partition signature not found (0x%x)\n", part_hdr->start_signature);
            halt_system();
        }
        prtos_conf_boot_partition_table[container.partition_table[e].id].flags |= PRTOS_PART_BOOT;
        prtos_conf_boot_partition_table[container.partition_table[e].id].hdr_phys_addr = (prtos_address_t)part_hdr;
        prtos_conf_boot_partition_table[container.partition_table[e].id].entry_point = pef_file.hdr->entry_point;
        prtos_conf_boot_partition_table[container.partition_table[e].id].image_start = (prtos_address_t)pef_file.hdr;
        prtos_conf_boot_partition_table[container.partition_table[e].id].img_size = pef_file.hdr->file_size;
        // Loading additional custom files
        min = (container.partition_table[e].num_of_custom_files > part_hdr->num_of_custom_files) ? part_hdr->num_of_custom_files
                                                                                                 : container.partition_table[e].num_of_custom_files;

        if (min > CONFIG_MAX_NO_CUSTOMFILES) {
            xprintf("[RSW] Error when parsing PEF custom files. num_of_custom_files > CONFIG_MAX_NO_CUSTOMFILES\n");
            halt_system();
        }

        prtos_conf_boot_partition_table[container.partition_table[e].id].num_of_custom_files = min;

        for (i = 0; i < min; i++) {
            if ((ret = parse_pef_file((prtos_u8_t *)(container.file_table[container.partition_table[e].custom_file_table[i]].offset + prtos_pef_container_ptr),
                                      &pef_custom_file)) != PEF_OK) {
                xprintf("[RSW] Error %d when parsing PEF file\n", ret);
                halt_system();
            }
            prtos_conf_boot_partition_table[container.partition_table[e].id].custom_file_table[i].start_addr = (prtos_address_t)pef_custom_file.hdr;
            prtos_conf_boot_partition_table[container.partition_table[e].id].custom_file_table[i].size = pef_custom_file.hdr->image_length;
            load_pef_custom_file(&pef_custom_file, &pef_file.custom_file_table[i]);
        }
    }

    xprintf("[RSW] Starting prtos at 0x%x\n", hpv_entry_point[0]);
    ((void (*)(void))ADDR2PTR(hpv_entry_point[0]))();
}
