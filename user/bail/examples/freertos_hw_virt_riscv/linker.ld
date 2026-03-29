ENTRY(_boot)

STACKTOP = 0x86200000;

SECTIONS
{
	/* Partition IPA base (must match prtos_cf.riscv64.xml Area start) */
	. = 0x86000000;
	__start = .;

	/* PRTOS image header (required by prtoseformat) */
	.prtos_image_hdr : { KEEP(*(.prtos_image_hdr)) }

	__text_start = .;
	.text :
	{
		KEEP(*(.text.boot))
		*(.text)
	}
	. = ALIGN(4096); /* align to page size */
	__text_end = .;

	__data_start = .;
	.data :
	{
		*(.data)
	}
	. = ALIGN(4096); /* align to page size */
	__data_end = .;

	__bss_start = .;
	.bss :
	{
		bss = .;
		*(.bss)
		*(COMMON)
	}
	. = ALIGN(4096); /* align to page size */
	__bss_end = .;
	__end = .;

	. = STACKTOP ; /* stack memory */
	stack_top = .;
}
__bss_size = (__bss_end - __bss_start)>>3;
