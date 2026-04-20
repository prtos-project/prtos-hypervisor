/*
 * Linker script for Linux guest partition image (Partition 0, LoongArch64).
 * Contains boot stub + Linux vmlinux.bin (embedded via .incbin).
 * GPA base: 0x80000000 (matches prtos_cf.loongarch64.xml Area start)
 */

ENTRY(_boot)

SECTIONS {
    . = 0x80000000;

    /* PRTOS image header (must be first for prtoseformat) */
    .prtos_image_hdr : {
        *(.prtos_image_hdr)
    }

    /* Boot stub code */
    . = ALIGN(0x1000);
    .text : {
        *(.text)
    }

    /* Linux kernel binary (2MB aligned) */
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
