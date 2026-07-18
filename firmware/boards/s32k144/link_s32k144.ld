MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 512K
  RAM  (rwx) : ORIGIN = 0x1FFF8000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) } > FLASH
  _sidata = LOADADDR(.data);
  .data : { _sdata = .; *(.data*) _edata = .; } > RAM AT> FLASH
  _siramfunc = LOADADDR(.ramfunc);
  .ramfunc : { _sramfunc = .; *(.ramfunc*) _eramfunc = .; } > RAM AT> FLASH
  .bss : { _sbss = .; *(.bss*) *(COMMON) _ebss = .; } > RAM
  . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .);
}
