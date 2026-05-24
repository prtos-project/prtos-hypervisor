/*
 * Linker script for Guest Partition (Partition 1) - LoongArch64 Virtio Frontend.
 * GPA base: 0xA0000000 (matches resident_sw.xml Partition 1 Area start)
 */

ENTRY(_boot)

SECTIONS {
    . = 0xA0000000;

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

    . = ALIGN(0x200000);
    .linux_image_guest : {
        *(.linux_image_guest)
    }

    . = ALIGN(0x1000);
    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
