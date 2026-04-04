/*
 * Linker script for System Partition (Partition 0) - Virtio Backend.
 * Contains PRTOS image header + boot stub + bzImage + initrd overlay.
 *
 * Partition physical area: start=0x6000000, size=128MB (ends at 0xDFFFFFF)
 * (matches prtos_cf.amd64.xml Partition 0 Area start)
 *
 * Memory layout:
 *   0x6000000   PRTOS image header (required by prtoseformat)
 *   0x6001000   Boot stub code (.text)
 *   0x6002000   Read-only data (.rodata)
 *   0x6100000   bzImage (embedded, 1MB aligned, ~17MB)
 *   0xC000000   initrd CPIO overlay (placed high to avoid kernel decompression)
 *               Contains: /usr/bin/prtos_manager, /usr/bin/virtio_backend
 */

ENTRY(_boot)

OUTPUT_FORMAT("elf64-x86-64")

SECTIONS {
    . = 0x6000000;

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

    . = 0x6100000;
    .bzimage_sys : {
        *(.bzimage_sys)
    }

    /* Place initrd at 192MB mark (0xC000000) - well above kernel decompression
     * area. The kernel decompressor targets low physical addresses (~0x1000000
     * relative to partition base). Placing initrd 96MB into the 128MB partition
     * ensures it survives decompression intact.
     */
    . = 0xC000000;
    .initrd_sys : {
        *(.initrd_sys)
    }

    . = ALIGN(0x1000);
    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
