    .section .text.start
    .globl _start

    # Exception vectors
_start:
    b reset
    b undef
    b softint
    b pref_abort
    b data_abort
    b rsrvd
    b irq
    b fiq

reset:
    # Disable interrupts.
    mrs r0, CPSR
    orr r0, r0, #0xC0
    msr CPSR, r0

    # Give ourselves 64k of program space, stick stack at the top of that.
    ldr sp, stack_pointer

    # Load up our timer init value.
    ldr r9, jiffies_per_second

    # Start the 1ms periodic timer.
    ldr r8, timer_control_regs
    str r9, [r8, #0x00]
    mov r9, #0x40
    str r9, [r8, #0x0C]

    # Clear all leftover interrupts from BIOS.
    ldr r9, [r8, #0x10]
    str r9, [r8, #0x14]

    # Set up interrupt levels so we get the right bits later. Sets the level 0
    # bit (#1 in fiq) to all interrupts except for the timer A interrupt. Sets
    # the level 1 bit (#2 in fiq) to the timer A interrupt. Sets the level 2
    # interrupt bit to no interrupts.
    mov r9, #0xBF
    str r9, [r8, #0x18]
    mov r9, #0x40
    str r9, [r8, #0x1C]
    mov r9, #0
    str r9, [r8, #0x20]

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
    # Enable interrupts.
    mrs r0, CPSR
    bic r0, r0, #0xC0
    msr CPSR, r0

    # Call main
    bl main

done:
    # Infinite loop
    b done

undef:
softint:
    # Dummy handler for undefined instruction and soft interrupt.
    movs pc, lr

pref_abort:
rsrvd:
irq:
    # Dummy handler for IRQ, prefetch abort, and reserved.
    sub lr, lr, #4
    movs pc, lr

data_abort:
    # Dummy handler for data abort.
    sub lr, lr, #8
    movs pc, lr

fiq:
    # ARM FIQ gives us shadow registers R8-R14, so it is safe
    # to clobber them without saving their originals first.

    # First, grab the interrupt source (timer, bus req, unknown).
    ldr r8, interrupt_type_reg
    ldr r9, [r8]
    and r9, r9, #7

    # Bus request, stay in FIQ until SH-4 is done mucking with our RAM.
    cmp r9, #1
    beq fiq_busreq

    # Timer interrupt, adjust our timer and reschedule.
    cmp r9, #2
    beq fiq_timer

    # Unknown source, shouldn't be possible, but let's ignore it.
fiq_done:
    # Clear interrupt
    ldr r8, interrupt_clear_reg
    mov r9, #1

    # Gotta do this 4 times to clear the interrupt.
    strb r9, [r8]
    strb r9, [r8]
    strb r9, [r8]
    strb r9, [r8]

    # Back to where we were when FIQ struck.
    sub lr, lr, #4
    movs pc, lr

fiq_timer:
    # Increment our timer variable.
    adr r8, millisecond_timer
    ldr r9, [r8]
    add r9, r9, #1
    str r9, [r8]

    # Load our timer amount (enough to hit 1000 counts per second).
    ldr r10, jiffies_per_second

    # Increment our millisecond adjust value. The master clock
    # for timers is 44100hz so we cannot divide that evenly to
    # get 1000 time slices per second. So, once every 10
    # iterations we add one to the calculated time slice to fix
    # the drift.
    adr r8, millisecond_adjust
    ldr r9, [r8]
    add r9, r9, #1
    str r9, [r8]

    # See if we hit 10.
    cmp r9, #10
    bne timer_reload

    # We did, reset it to zero, add one to our divisor.
    mov r9, #0
    str r9, [r8]
    sub r10, r10, #1

timer_reload:
    # Kick off a new timer.
    ldr r8, timer_control_regs
    str r10, [r8, #0x00]
    mov r9, #0x40
    str r9, [r8, #0x14]

    # Exit FIQ.
    b fiq_done

fiq_busreq:
    ldr r8, busreq_control_reg

fiq_busreq_loop:
    # Wait until bit 0x100 goes back to 0.
    ldr r9, [r8]
    and r9, r9, #0x100
    cmp r9, #0
    bne fiq_busreq_loop

    # Exit FIQ.
    b fiq_done

    .align 4

stack_pointer:
    .long 0x20000

bss_start_addr:
    # Location of .bss section we must zero
    .long __bss_start

bss_end_addr:
    # Location of end of ROM where we stop zeroin
    .long __bss_end

interrupt_type_reg:
    .long 0x00802d00

interrupt_clear_reg:
    .long 0x00802d04

timer_control_regs:
    .long 0x00802890

busreq_control_reg:
    .long 0x00802808

jiffies_per_second:
    .long 256 - (44100 / 1000)

    .globl millisecond_timer

millisecond_timer:
    .long 0

millisecond_adjust:
    .long 0

    .end
