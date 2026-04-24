/*
 * Linker script for Linux guest partition image (Partition 0, RISC-V 64).
 * Contains boot stub + DTB + Linux Image (embedded via .incbin).
 * IPA base: 0x90000000 (matches prtos_cf.riscv64.xml Area start)
 */

ENTRY(_boot)

SECTIONS {
    . = 0x90000000;

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

    /* Linux Image (2MB aligned per boot protocol) */
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
