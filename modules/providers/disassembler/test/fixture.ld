/* Linker script for fixture.elf: see fixture.s for the rebuild command. */

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1K
}

SECTIONS
{
  .text : { *(.text*) } > FLASH
}
