SECTIONS
{
    . = 0x0;
    .text : {
        *(.text.start)
        *(.text)
    }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
