#include <string.h>  // string services provided by the libxal library
#include <stdio.h>   // stdio services provided by the libxal library
#include <prtos.h>   // hypercall services provided by the libprtos library
#include <irqs.h>    // virtual irq services provided by the libxal library
#include <config.h>

#define PRINT(...)                             \
    do {                                       \
        printf("[%d] ", PRTOS_PARTITION_SELF); \
        printf(__VA_ARGS__);                   \
    } while (0)

// prtos_u8_t customFile[2048];                                         /* <-- BAD. Watch .bss zeroing */
__attribute__((section(".custom"))) prtos_u8_t customFile[2048] = {0}; /* <-- OK  */
static prtos_u8_t _page_table[PAGE_SIZE * CONFIG_SIZE_PAGE_TABLE] __attribute__((aligned(PAGE_SIZE))) __attribute__((section(".bss.noinit")));

struct prtos_image_hdr prtos_image_hdr __PRTOS_IMAGE_HDR = {
    .start_signature = PRTOS_EXEC_PARTITION_MAGIC,
    .compilation_prtos_abi_version = PRTOS_SET_VERSION(PRTOS_ABI_VERSION, PRTOS_ABI_SUBVERSION, PRTOS_ABI_REVISION),
    .compilation_prtos_api_version = PRTOS_SET_VERSION(PRTOS_API_VERSION, PRTOS_API_SUBVERSION, PRTOS_API_REVISION),
    .page_table = (prtos_address_t)_page_table,
    .page_table_size = CONFIG_SIZE_PAGE_TABLE * PAGE_SIZE,
    .num_of_custom_files = 1,
    .custom_file_table =
        {
            [0] =
                (struct xef_custom_file){
                    .sAddr = (prtos_address_t)customFile,
                    .size = 2048,
                },
        },
    .end_signature = PRTOS_EXEC_PARTITION_MAGIC,
};

void partition_main(void) {
    PRINT("----- Contents of the Custom File -----\n");
    PRINT("%s\n", customFile);
    PRINT("----- Contents of the Custom File -----\n");
    PRINT("Verification Passed\n");
    PRINT("Halting\n");
    prtos_halt_partition(PRTOS_PARTITION_SELF);
}
