    .section .text.start
    .globl start

start:
    bra bss_zero
    mov #0,r3

bss_zero:
    # Now, we need to initialize the stack (we don't have a frame pointer)
    # to the top of memory.
    mov.l stack_addr,r15

    # Now, zero out the .bss section.
    mov.l bss_start_addr,r0
    mov.l bss_end_addr,r1
    mov #0,r2

bss_zero_loop:
    mov.l r2,@r0
    add #4,r0
    cmp/ge r0,r1
    bt bss_zero_loop

    # Now we must run the executable.
    mov.l main_addr,r0
    nop
    jmp @r0
    nop

    .align 4

stack_addr:
    # Location of stack
    .long 0x0E000000

bss_start_addr:
    # Location of .bss section we must zero
    .long _edata

bss_end_addr:
    # Location of end of ROM where we stop zeroing
    .long _end

main_addr:
    # Location of main
    .long __enter
