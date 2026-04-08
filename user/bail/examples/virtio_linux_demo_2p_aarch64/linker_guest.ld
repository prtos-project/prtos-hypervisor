/*
 * Linker script for Guest Partition (Partition 1) - AArch64 Virtio Frontend.
 * Contains PRTOS image header + boot stub + DTB + Image + guest rootfs overlay.
 *
 * IPA base: 0x18000000 (matches resident_sw.xml Partition 1 Area start)
 */

ENTRY(_boot)

SECTIONS {
    . = 0x18000000;

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

    . = ALIGN(0x1000);
    .dtb_guest : {
        *(.dtb_guest)
    }

    . = ALIGN(0x200000);
    .linux_image_guest : {
        *(.linux_image_guest)
    }

    /* Place guest initrd at 64MB offset from partition base (0x1C000000) */
    . = 0x1C000000;
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
