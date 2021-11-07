SECTIONS
{
    . = 0x0;
    .text : {
        *(.text.start)
        *(.text)
    }
    .data : { *(.data) }
    __bss_start = .; PROVIDE (_bss_start = .);
    .bss : { *(.bss) }
    . = ALIGN(32 / 8);
    . = ALIGN(32 / 8);
    __bss_end = .; PROVIDE (_bss_end = .);
}
