/*
 * Minimal PRTOS image header for FreeRTOS hw-virt partition (native, unmodified).
 *
 * Required by prtoseformat to convert ELF -> PEF.
 * The page_table field is unused for hw-virt partitions (stage-2 MMU
 * is managed by the hypervisor, not the partition).
 */
#include <stdint.h>

#define PRTOS_EXEC_PARTITION_MAGIC 0x24584d69
#define PRTOS_SET_VERSION(v, s, r) (((v & 0xFF) << 16) | ((s & 0xFF) << 8) | (r & 0xFF))
#define CONFIG_MAX_NO_CUSTOMFILES 3

struct pef_custom_file {
    uint64_t start_addr;
    uint64_t size;
} __attribute__((packed));

struct prtos_image_hdr {
    uint32_t start_signature;
    uint32_t compilation_prtos_abi_version;
    uint32_t compilation_prtos_api_version;
    uint64_t page_table;
    uint64_t page_table_size;
    uint32_t num_of_custom_files;
    struct pef_custom_file custom_file_table[CONFIG_MAX_NO_CUSTOMFILES];
    uint32_t end_signature;
} __attribute__((packed));

struct prtos_image_hdr prtos_image_hdr __attribute__((section(".prtos_image_hdr"))) = {
    .start_signature = PRTOS_EXEC_PARTITION_MAGIC,
    .compilation_prtos_abi_version = PRTOS_SET_VERSION(1, 0, 0),
    .compilation_prtos_api_version = PRTOS_SET_VERSION(1, 0, 0),
    .page_table = 0,
    .page_table_size = 0,
    .num_of_custom_files = 0,
    .end_signature = PRTOS_EXEC_PARTITION_MAGIC,
};
