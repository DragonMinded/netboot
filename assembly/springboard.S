.section .text
.globl start

start:
# Grab the patch location to jump to
mova @(8,pc),r0
mov.l @r0,r0
jmp @r0
nop

# Four bytes of data will be added here representing the jump point by the patch compiler.
.byte 0xDD
.byte 0xDD
.byte 0xDD
.byte 0xDD
