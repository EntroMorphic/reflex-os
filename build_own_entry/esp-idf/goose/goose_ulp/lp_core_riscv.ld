       
       
       
ENTRY(reset_vector)
MEMORY
{
    vector_table(RX) : ORIGIN = (0x50000000) , LENGTH = 0x80
    ram(RWX) : ORIGIN = (0x50000000) + 0x80, LENGTH = (2048 & ~(0x8 - 1)) - 0x80 - 0x10
    shared_mem_ram(RW) : ORIGIN = (0x50000000) + (2048 & ~(0x8 - 1)) - 0x10, LENGTH = 0x10
}
SECTIONS
{
    .vector.text :
    {
        __mtvec_base = .;
        KEEP (*(.init.vector .init.vector.*))
    } > vector_table
    . = ORIGIN(ram);
    .text ALIGN(4):
    {
        *(.text.vectors)
        *(.text)
        *(.text*)
    } >ram
    .rodata ALIGN(4):
    {
        *(.rodata)
        *(.rodata*)
    } > ram
    .data ALIGN(4):
    {
        *(.data)
        *(.data*)
        *(.sdata)
        *(.sdata*)
    } > ram
    .bss ALIGN(4) :
    {
        *(.bss)
        *(.bss*)
        *(.sbss)
        *(.sbss*)
        PROVIDE(end = .);
    } >ram
    __stack_top = ORIGIN(ram) + LENGTH(ram);
    . = ORIGIN(shared_mem_ram);
    .shared_mem (ALIGN(4)) :
    {
        KEEP(*(.shared_mem))
    } > shared_mem_ram
}
