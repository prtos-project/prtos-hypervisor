/*
 * $FILE$
 * 
 * ld script to build aarch64
 * 
 * www.prtos.org
 */

/*OUTPUT_FORMAT("binary")*/
OUTPUT_FORMAT("elf64-littleaarch64", "elf64-bigaarch64",
	      "elf64-littleaarch64")
OUTPUT_ARCH("aarch64")
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

