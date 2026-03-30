ENTRY(_start)

SECTIONS
{
	/* Load at 1MB (standard location for multiboot kernels) */
	. = 0x100000;
	__start = .;

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
