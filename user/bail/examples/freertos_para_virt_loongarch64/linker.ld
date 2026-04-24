/* Linker script for FreeRTOS para-virt on PRTOS LoongArch64 */
OUTPUT_ARCH(loongarch)
ENTRY(_boot)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x06000000, LENGTH = 2M
}

SECTIONS
{
    . = 0x06000000;

    .text : {
        *(.text.boot)
        *(.text*)
    } > RAM

    .rodata : {
        *(.rodata*)
    } > RAM

    .data : {
        *(.data*)
    } > RAM

    . = ALIGN(16);
    __bss_start = .;
    .bss : {
        *(.bss*)
        *(COMMON)
    } > RAM
    __bss_end = .;

    . = ALIGN(16);
    . = . + 0x10000;  /* 64KB stack */
    stack_top = .;
}
