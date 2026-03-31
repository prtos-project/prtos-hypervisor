ENTRY(_start)

SECTIONS
{
	/* Partition physical address (must match prtos_cf.amd64.xml Area start) */
	. = 0x6000000;
	__start = .;

	/* PRTOS image header (required by prtoseformat) */
	.prtos_image_hdr : { KEEP(*(.prtos_image_hdr)) }

	.multiboot : {
		KEEP(*(.multiboot))
	}

	.note.PVH : {
		KEEP(*(.note.PVH))
	}

	__text_start = .;
	.text : {
		*(.text)
	}
	. = ALIGN(4096);
	__text_end = .;

	__rodata_start = .;
	.rodata : {
		*(.rodata*)
	}
	. = ALIGN(4096);
	__rodata_end = .;

	__data_start = .;
	.data : {
		*(.data)
	}
	. = ALIGN(4096);
	__data_end = .;

	__bss_start = .;
	.bss : {
		*(.bss)
		*(COMMON)
	}
	. = ALIGN(4096);
	__bss_end = .;
	__end = .;
}
