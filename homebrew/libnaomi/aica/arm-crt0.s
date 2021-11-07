    .section .text.start
    .globl _start

    # Exception vectors
_start:
    b   reset
    b   undef
    b   softint
    b   pref_abort
    b   data_abort
    b   rsrvd
    b   irq
    b   fiq

reset:
    # Disable IRQ and FIQ
    mrs r0, CPSR
    orr r0, r0, #0xC0
    msr CPSR, r0

    # TODO: Better stack location, unclear where at this point.
    mov sp, #0xf000

    # Call main
    bl main

done:
    # Infinite loop
    b done
    
    # No-op handlers for the remaining vectors
undef:
softint:
    movs pc, r14

pref_abort:
irq:
fiq:
rsrvd:
    subs pc, r14, #4
    
data_abort:
    subs pc, r14, #8

    .end
