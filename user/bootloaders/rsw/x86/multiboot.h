/*
 * FILE: multiboot.h
 *
 * Multiboot header file
 *
 * The multiboot specification can be found at:
 * http://www.gnu.org/software/grub/manual/multiboot/
 *
 * www.prtos.org
 */

#ifndef _MULTIBOOT_H_
#define _MULTIBOOT_H_

/* The magic number for the Multiboot header. */
#define __MULTIBOOT_HEADER_MAGIC 0x1BADB002

/* The flags for the Multiboot header. */
#define __MULTIBOOT_HEADER_FLAGS 0x3

/* The magic number passed by a Multiboot-compliant boot loader. */
#define __MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#ifndef __ASSEMBLY__

/* The section header table for ELF. */
typedef struct elf_section_header_table {
    unsigned long num;
    unsigned long size;
    unsigned long addr;
    unsigned long shndx;
} elf_section_header_table_t;

/* The Multiboot information. */
typedef struct multiboot_info {
    unsigned long flags;
    unsigned long mem_lower;
    unsigned long mem_upper;
    unsigned long boot_device;
    unsigned long cmdline;
    unsigned long mods_count;
    unsigned long mods_addr;
    elf_section_header_table_t elf_sec;
    unsigned long mmap_length;
    unsigned long mmap_addr;
} multiboot_info_t;

/* The module structure. */
typedef struct module {
    unsigned long mod_start;
    unsigned long mod_end;
    unsigned long string;
    unsigned long reserved;
} multiboot_module_t;

/* The memory map. Be careful that the offset 0 is base_addr_low
   but no size. */
typedef struct memory_map {
    unsigned long size;
    unsigned long base_addr_low;
    unsigned long base_addr_high;
    unsigned long length_low;
    unsigned long length_high;
    unsigned long type;
} multiboot_memory_map_t;

#endif  // !__ASSEMBLY__
#endif
