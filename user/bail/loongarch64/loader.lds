/*
 * FILE: loader.lds
 *
 * ld script to build LoongArch64 BAIL partitions
 *
 * www.prtos.org
 */

OUTPUT_FORMAT("elf64-loongarch", "elf64-loongarch", "elf64-loongarch")
OUTPUT_ARCH("loongarch")
ENTRY(start)

SECTIONS
{
  _sguest = .;
  .text ALIGN (8): {
	. = ALIGN(4K);
	_sguest = .;
  	*(.text.init)
  	*(.text)
  }

  .rodata ALIGN (8) : {
	*(.rodata)
  	*(.rodata.*)
	*(.rodata.*.*)
  }

  .data ALIGN (8) : {
	_sdata = .;
	*(.data)
	_edata = .;
  }

  .bss ALIGN (8) : {
	*(.bss.noinit)
	_sbss = .;
	*(COMMON)
	*(.bss)
	_ebss = .;
  }

  _eguest = .;

  /DISCARD/ :
  {
	*(.note)
	*(.comment*)
  }
}
