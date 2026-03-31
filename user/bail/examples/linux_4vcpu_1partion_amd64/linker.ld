/*
 * Linker script for Linux guest partition image (amd64/x86_64).
 * Contains PRTOS image header + boot stub + bzImage (embedded via .incbin).
 *
 * Partition physical area: start=0x6000000, size=256MB
 * (matches prtos_cf.amd64.xml Area start)
 *
 * Memory layout:
 *   0x6000000   PRTOS image header (required by prtoseformat)
 *   0x6001000   Boot stub code (.text)
 *   0x6002000   Read-only data (.rodata)
 *   0x6100000   bzImage (embedded, 1MB aligned)
 */

ENTRY(_boot)

OUTPUT_FORMAT("elf64-x86-64")

SECTIONS {
    . = 0x6000000;

    /* PRTOS image header (must be first for prtoseformat) */
    .prtos_image_hdr : {
        *(.prtos_image_hdr)
    }

    /* Boot stub code */
    . = ALIGN(0x1000);
    .text : {
        *(.text)
    }

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata*)
    }

    /* bzImage embedded at 1MB offset from partition start */
    . = 0x6100000;
    .bzimage : {
        *(.bzimage)
    }

    . = ALIGN(0x1000);
    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
