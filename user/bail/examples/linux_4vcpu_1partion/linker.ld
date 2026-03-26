/*
 * Linker script for Linux guest partition image.
 * Contains boot stub + DTB + Linux Image (embedded via .incbin).
 *
 * IPA base: 0x10000000 (matches prtos_cf.aarch64.xml Area start)
 * PA base:  0x50000000 (= IPA + 0x40000000, after PRTOS core at 0x49000000)
 */

ENTRY(_boot)

SECTIONS {
    . = 0x10000000;

    /* PRTOS image header (must be first for prtoseformat) */
    .prtos_image_hdr : {
        *(.prtos_image_hdr)
    }

    /* Boot stub code */
    . = ALIGN(0x1000);
    .text : {
        *(.text)
    }

    /* Device tree blob */
    . = ALIGN(0x1000);
    .dtb : {
        *(.dtb)
    }

    /* Linux Image (2MB aligned per ARM64 boot protocol) */
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
