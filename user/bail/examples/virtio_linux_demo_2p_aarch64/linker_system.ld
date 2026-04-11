/*
 * Linker script for System Partition (Partition 0) - AArch64 Virtio Backend.
 * Contains PRTOS image header + boot stub + DTB + Image + initrd overlay.
 *
 * IPA base: 0x10000000 (matches resident_sw.xml Partition 0 Area start)
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
    .rodata : {
        *(.rodata*)
    }

    . = ALIGN(0x1000);
    .dtb_sys : {
        *(.dtb_sys)
    }

    . = ALIGN(0x200000);
    .linux_image_sys : {
        *(.linux_image_sys)
    }

    /* Place initrd at 64MB offset from partition base (0x14000000).
     * Well above kernel decompression area. With 128MB partition,
     * this gives 64MB for kernel + 64MB for initrd.
     */
    . = 0x14000000;
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
