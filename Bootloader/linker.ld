MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 16K
  RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 48K
}

ENTRY(Reset_Handler)

SECTIONS
{
  .text :
  {
    KEEP(*(.isr_vector))
    /* iap_run at 0x08000200 so the app can jump without linking the BL. */
    . = ORIGIN(FLASH) + 0x200;
    KEEP(*(.iap_entry))
    *(.text*)
    *(.rodata*)
    _etext = .;
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    _sdata = .;
    *(.data*)
    _edata = .;
  } >RAM AT> FLASH

  .bss :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    . = . + 0x400;
    . = ALIGN(8);
  } >RAM

  _estack = ORIGIN(RAM) + LENGTH(RAM);
}
