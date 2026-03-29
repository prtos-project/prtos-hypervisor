/*
 * ld script to build amd64
 *
 * www.prtos.org
 */

OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH("i386:x86-64")
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
