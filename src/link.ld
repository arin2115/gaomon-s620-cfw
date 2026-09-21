/* Gaomon S620 custom firmware: app image at 0x08004000 (resident GD32 DFU bootloader occupies 0x08000000-0x08003FFF).
   0x0800F800-0x0800FFFF holds the saved settings (settings.c). RAM is limited to 4 KB until the real size is verified. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08004000, LENGTH = 0xB000
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 0x1000
}

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    _sidata = LOADADDR(.data);
    .data : { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss  : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    _estack = ORIGIN(RAM) + LENGTH(RAM);
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
