OUTPUT_FORMAT("elf64-littleaarch64", "elf64-bigaarch64",
       "elf64-littleaarch64")
OUTPUT_ARCH("aarch64")
ENTRY(_start)
START_ADDR = 0x40200000;
SECTIONS
{
  . = START_ADDR;
  _srorsw = .;
  .text.boot : {
    KEEP(*(.text.boot))
  }
  .text ALIGN (0x1000): {
   *(.text)
 . = ALIGN(8);
 prtos_pef_container_ptr = . ;
  QUAD(_scontainer);
  }
  .rodata ALIGN (4) : {
 *(.rodata)
   *(.rodata.*)
 *(.rodata.*.*)
  }
  prev = .;
  . = 0x40280000;
  .container ALIGN(8) : {
  _scontainer = . ;
  *(.container)
  }
  . = prev;
  _erorsw = .;
  _srsw = .;
  .data ALIGN (4) : {
  _sdata = .;
 _sdata_lma = LOADADDR(.data);
 *(.data)
 _edata = .;
 hpv_entry_point = .;
  QUAD(0);
  }
  .bss ALIGN (4) : {
  _sbss = .;
  *(COMMON)
  *(.bss)
  _ebss = .;
  }
   _ersw = .;
  /DISCARD/ :
  {
 *(.note)
 *(.comment*)
        *(.eh_frame)
  }
}
