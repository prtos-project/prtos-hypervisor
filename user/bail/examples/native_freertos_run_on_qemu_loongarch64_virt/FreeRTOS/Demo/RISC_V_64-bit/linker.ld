ENTRY(_boot)

STACKTOP = 0x00400000;

SECTIONS
{
	/* LoongArch64 QEMU virt: BIOS loads firmware at 0x1c000000,
	 * but for hw-virt under PRTOS the IPA base is set by the
	 * hw_virt directory's linker.ld. This is for standalone use. */
	. = 0x00200000;
	__start = .;
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
