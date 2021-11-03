    .section .text.start
    .globl start

start:
    # We must run the executable.
    mov.l main_addr,r0
    nop
    jmp @r0
    nop

    .align 4

main_addr:
    # Location of main
    .long __enter
