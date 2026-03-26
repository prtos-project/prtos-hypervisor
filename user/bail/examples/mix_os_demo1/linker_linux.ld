/*
 * Linker script for Linux guest partition image (Partition 1).
 * IPA base: 0x10000000 (matches prtos_cf.aarch64.xml Area start)
 */

ENTRY(_boot)

SECTIONS {
    . = 0x10000000;

    .prtos_image_hdr : {
        *(.prtos_image_hdr)
    }

    . = ALIGN(0x1000);
    .text : {
        *(.text)
    }

    . = ALIGN(0x1000);
    .dtb : {
        *(.dtb)
    }

    . = ALIGN(0x200000);
    .linux_image : {
        *(.linux_image)
    }

    . = ALIGN(0x1000);
    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
