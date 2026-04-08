/*
 * Linker script for Guest Partition (Partition 1) - Virtio Frontend.
 * Contains PRTOS image header + boot stub + bzImage + guest rootfs overlay.
 *
 * Partition physical area: start=0xE000000, size=128MB
 * (matches prtos_cf.amd64.xml Partition 1 Area start)
 *
 * Memory layout:
 *   0xE000000  PRTOS image header (required by prtoseformat)
 *   0xE001000  Boot stub code (.text)
 *   0xE002000  Read-only data (.rodata)
 *   0xE100000  bzImage (embedded, 1MB aligned)
 *   0x12000000 initrd CPIO overlay (guest test scripts)
 */

ENTRY(_boot)

OUTPUT_FORMAT("elf64-x86-64")

SECTIONS {
    . = 0xE000000;

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

    . = 0xE100000;
    .bzimage_guest : {
        *(.bzimage_guest)
    }

    /* Place guest initrd at 0x12000000 - 64MB offset from partition base */
    . = 0x12000000;
    .initrd_guest : {
        *(.initrd_guest)
    }

    . = ALIGN(0x1000);
    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
