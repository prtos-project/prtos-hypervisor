/*
 * FILE: hdr.c
 *
 * PRTOS header
 *
 * www.prtos.org
 */

#include <prtos.h>
#include <config.h>
#include <prtos_inc/arch/paging.h>

static prtos_u8_t _page_table[PAGE_SIZE * CONFIG_SIZE_PAGE_TABLE] __attribute__((aligned(PAGE_SIZE))) __attribute__((section(".bss.noinit")));

struct prtos_image_hdr prtos_image_hdr __PRTOS_IMAGE_HDR = {
    .start_signature = PRTOS_EXEC_PARTITION_MAGIC,
    .compilation_prtos_abi_version = PRTOS_SET_VERSION(PRTOS_ABI_VERSION, PRTOS_ABI_SUBVERSION, PRTOS_ABI_REVISION),
    .compilation_prtos_api_version = PRTOS_SET_VERSION(PRTOS_API_VERSION, PRTOS_API_SUBVERSION, PRTOS_API_REVISION),
    .page_table = (prtos_address_t)_page_table,
    .page_table_size = CONFIG_SIZE_PAGE_TABLE * PAGE_SIZE,
    .num_of_custom_files = 0,
    .end_signature = PRTOS_EXEC_PARTITION_MAGIC,
};
