/* Text must not overlap D-side VMA.
 * CoreMark -Os text is ~7 KiB, so DMEM starts at 8 KiB. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  IMEM (rx) : ORIGIN = 0x00000000, LENGTH = 8K
  DMEM (rw) : ORIGIN = 0x00002000, LENGTH = 24K
}

SECTIONS
{
  .text :
  {
    . = ALIGN(4);
    KEEP(*(.text.startup))
    *(.text .text.*)
    . = ALIGN(4);
    KEEP(*(.text.end))
    _etext = .;
  } > IMEM

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
    . = ALIGN(4);
  } > DMEM

  .data :
  {
    . = ALIGN(4);
    _data_start = .;
    *(.data .data.*)
    *(.sdata .sdata.*)
    . = ALIGN(4);
    _data_end = .;
  } > DMEM

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _bss_start = .;
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
    . = ALIGN(4);
    _bss_end = .;
  } > DMEM

  _stack_top = 0x00008000;

  /DISCARD/ :
  {
    *(.eh_frame*)
    *(.comment)
    *(.riscv.attributes)
    *(.note*)
  }
}
