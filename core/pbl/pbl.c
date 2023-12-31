/*
 * FILE: pbl.c
 *
 * Partition loader code
 *
 * www.prtos.org
 */

#undef _PRTOS_KERNEL_
#include <pef.h>
#include <prtos.h>

partition_control_table_t *part_ctrl_table_ptr;

static int vaddr_to_paddr(void *memory_areas, prtos_s32_t num_of_areas, prtos_address_t v_addr, prtos_address_t *p_addr) {
    *p_addr = v_addr;
    return 0;
}

prtos_address_t main_pbl(void) {
    struct pef_file pef_file, pef_custom_file;
    struct prtos_image_hdr *part_hdr;
    prtos_s32_t ret, i;

    prtos_u8_t *img = (prtos_u8_t *)part_ctrl_table_ptr->image_start;
    if ((ret = parse_pef_file(img, &pef_file)) != PEF_OK) return 0;
    part_hdr = load_pef_file(&pef_file, vaddr_to_paddr, 0, 0);

    img = (prtos_u8_t *)((part_ctrl_table_ptr->image_start + pef_file.hdr->file_size) & (~(PAGE_SIZE - 1))) + PAGE_SIZE;

    for (i = 0; i < part_hdr->num_of_custom_files; i++) {
        if ((ret = parse_pef_file((prtos_u8_t *)img, &pef_custom_file)) != PEF_OK) return 0;
        load_pef_custom_file(&pef_custom_file, &pef_file.custom_file_table[i]);
    }

    return pef_file.hdr->entry_point;
}
