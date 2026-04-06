ENTRY(_boot)

STACKTOP = 0x6200000;

SECTIONS
{
	/* Partition 1 IPA base: 0x6000000 (2MB area) */
	. = 0x6000000;
	__start = .;

	.prtos_image_hdr : { KEEP(*(.prtos_image_hdr)) }

	__text_start = .;
	.text :
	{
		KEEP(*(.vectors))
		KEEP(*(.text.boot))
		*(.text)
	}
	. = ALIGN(4096);
	__text_end = .;

	__data_start = .;
	.data :
	{
		*(.data)
	}
	. = ALIGN(4096);
	__data_end = .;

	__bss_start = .;
	.bss :
	{
		bss = .;
		*(.bss)
	}
	. = ALIGN(4096);
	__bss_end = .;
	__end = .;

	. = STACKTOP ;
	stack_top = .;
}
__bss_size = (__bss_end - __bss_start)>>3;
