/*
 * Linker script for System Partition (Partition 0) - LoongArch64 Virtio Backend.
 * GPA base: 0x80000000 (matches resident_sw.xml Partition 0 Area start)
 */

ENTRY(_boot)

SECTIONS {
    . = 0x80000000;

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
    .linux_image_sys : {
        *(.linux_image_sys)
    }

    /* Place initrd at 64MB offset from partition base (0x8C000000) */
    . = 0x8C000000;
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
