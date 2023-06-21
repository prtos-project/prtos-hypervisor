/*
 * FILE: prtosef.h
 *
 * prtos's executable format
 *
 * www.prtos.org
 */

#ifndef _PRTOS_PRTOSEF_H_
#define _PRTOS_PRTOSEF_H_
/* build version number */
#define PRTOS_SET_VERSION(_ver, _subver, _rev) ((((_ver)&0xFF) << 16) | (((_subver)&0xFF) << 8) | ((_rev)&0xFF))
#define PRTOS_GET_VERSION(_v) (((_v) >> 16) & 0xFF)
#define PRTOS_GET_SUBVERSION(_v) (((_v) >> 8) & 0xFF)
#define PRTOS_GET_REVISION(_v) ((_v)&0xFF)

#ifndef __ASSEMBLY__

#include __PRTOS_INCFLD(linkage.h)

struct xef_custom_file {
    prtos_address_t sAddr;
    prtos_u_size_t size;
} __PACKED;

#define __PRTOS_HDR __attribute__((section(".prtos_hdr")))
#define __PRTOS_IMAGE_HDR __attribute__((section(".prtos_image_hdr")))

struct prtos_hdr {
#define PRTOS_EXEC_HYP_MAGIC 0x24584d68
    prtos_u32_t start_signature;
    prtos_u32_t compilation_prtos_abi_version;  // PRTOS's abi version
    prtos_u32_t compilation_prtos_api_version;  // PRTOS's api version
    prtos_u32_t num_of_custom_files;
    struct xef_custom_file custom_file_table[CONFIG_MAX_NO_CUSTOMFILES];
    prtos_u32_t end_signature;
} __PACKED;

struct prtos_image_hdr {
#define PRTOS_EXEC_PARTITION_MAGIC 0x24584d69
    prtos_u32_t start_signature;
    prtos_u32_t compilation_prtos_abi_version;  // PRTOS's abi version
    prtos_u32_t compilation_prtos_api_version;  // PRTOS's api version
                                                /* page_table is unused when MPU is set */
    prtos_address_t page_table;                 // Physical address
                                                /* page_table_size is unused when MPU is set */
    prtos_u_size_t page_table_size;
    prtos_u32_t num_of_custom_files;
    struct xef_custom_file custom_file_table[CONFIG_MAX_NO_CUSTOMFILES];
    prtos_u32_t end_signature;
} __PACKED;

struct prtos_exec_file {
    prtos_address_t offset;
    prtos_u_size_t size;
    prtos_address_t name_offset;
} __PACKED;

struct prtos_exec_partition {
    prtos_s32_t id;
    prtos_s32_t file;
    prtos_u32_t num_of_custom_files;
    prtos_s32_t custom_file_table[CONFIG_MAX_NO_CUSTOMFILES];
} __PACKED;

#define PRTOS_DIGEST_BYTES 16
#define PRTOS_PAYLOAD_BYTES 16

struct prtos_exec_container_hdr {
    prtos_u32_t signature;
#define PRTOS_PACKAGE_SIGNATURE 0x24584354  // $XCT
    prtos_u32_t version;
#define PRTOSPACK_VERSION 1
#define PRTOSPACK_SUBVERSION 0
#define PRTOSPACK_REVISION 0
    prtos_u32_t flags;
#define PRTOSEF_CONTAINER_DIGEST 0x1
    prtos_u8_t digest[PRTOS_DIGEST_BYTES];
    prtos_u32_t file_size;
    prtos_address_t part_table_offset;
    prtos_s32_t num_of_partitions;
    prtos_address_t file_table_offset;
    prtos_s32_t num_of_files;
    prtos_address_t string_table_offset;
    prtos_s32_t str_len;
    prtos_address_t file_data_offset;
    prtos_u_size_t file_data_length;
} __PACKED;

#define XEF_VERSION 1
#define XEF_SUBVERSION 0
#define XEF_REVISION 0

struct xef_hdr {
#define XEF_SIGNATURE 0x24584546
    prtos_u32_t signature;
    prtos_u32_t version;
#define XEF_DIGEST 0x1
#define XEF_COMPRESSED 0x4
#define XEF_RELOCATABLE 0x10

#define XEF_TYPE_MASK 0xc0
#define XEF_TYPE_HYPERVISOR 0x00
#define XEF_TYPE_PARTITION 0x40
#define XEF_TYPE_CUSTOMFILE 0x80

#define XEF_ARCH_SPARCv8 0x400
#define XEF_ARCH_MASK 0xff00
    prtos_u32_t flags;
    prtos_u8_t digest[PRTOS_DIGEST_BYTES];
    prtos_u8_t payload[PRTOS_PAYLOAD_BYTES];
    prtos_u_size_t file_size;
    prtos_address_t segment_table_offset;
    prtos_s32_t num_of_segments;
    prtos_address_t custom_file_table_offset;
    prtos_s32_t num_of_custom_files;
    prtos_address_t image_offset;
    prtos_u_size_t image_length;
    prtos_u_size_t deflated_image_length;
    prtos_address_t page_table;
    prtos_u_size_t page_table_size;
    prtos_address_t prtos_image_hdr;
    prtos_address_t entry_point;
} __PACKED;

struct xef_segment {
    prtos_address_t phys_addr;
    prtos_address_t virt_addr;
    prtos_u_size_t file_size;
    prtos_u_size_t deflated_file_size;
    prtos_address_t offset;
} __PACKED;

#endif

#endif
