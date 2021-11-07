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

    # Give ourselves 64k of program space, stick stack at the top of that.
    ldr sp, stack_pointer

    # Clear BSS section.
    ldr r0, bss_start_addr
    ldr r1, bss_end_addr
    mov r2, #0

bss_clear_loop:
    cmp r0, r1
    beq bss_clear_done
    str r2, [r0], #4
    b bss_clear_loop

bss_clear_done:
    # Call main
    bl main

done:
    # Infinite loop
    b done

stack_pointer:
    .long 0x10000

bss_start_addr:
    # Location of .bss section we must zero
    .long __bss_start

bss_end_addr:
    # Location of end of ROM where we stop zeroin
    .long __bss_end

undef:
softint:
    # No-op handlers for undefined instruction and softint.
    b softint

pref_abort:
irq:
rsrvd:
    b rsrvd

fiq:
    subs pc, r14, #4
    
data_abort:
    b data_abort

    .end
