/*
 * Linker script for Guest Partition (Partition 1) - Virtio Frontend.
 * Contains PRTOS image header + boot stub + bzImage.
 *
 * Partition physical area: start=0xe000000, size=128MB
 * (matches prtos_cf.amd64.xml Partition 1 Area start)
 *
 * Memory layout:
 *   0xe000000   PRTOS image header (required by prtoseformat)
 *   0xe001000   Boot stub code (.text)
 *   0xe002000   Read-only data (.rodata)
 *   0xe100000   bzImage (embedded, 1MB aligned)
 */

ENTRY(_boot)

OUTPUT_FORMAT("elf64-x86-64")

SECTIONS {
    . = 0xe000000;

    .prtos_image_hdr : {
        *(.prtos_image_hdr)
    }

    . = ALIGN(0x1000);
    .text : {
        *(.text)
    }

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata*)
    }

    . = 0xe100000;
    .bzimage_guest : {
        *(.bzimage_guest)
    }

    . = ALIGN(0x1000);
    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
