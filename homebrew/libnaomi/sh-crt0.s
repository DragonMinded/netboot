    .section .text.start
    .globl start

start:
    bra bss_zero
    mov #0,r3
start_test:
    mov #1,r3
    nop

bss_zero:
    # First, disable interrupts, set the regster bank to 0 (interrupts will
    # set it to 1 so we want to ensure we start in RB 0). We always run in
    # privileged mode since we don't want to build an entire kernel.
    mov.l sr_disable,r0
    ldc r0,sr

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

    # Now, we need to enable cache since the BIOS disables it
    # before calling into ROM space. So, get ourselves into P2
    # region so its safe to enable cache. Grab the address of
    # setup_cache (exactly 16 bytes forward), mask off the physical
    # address bits, and jump to it.
    mova @((setup_cache-.),pc),r0
    mov.l phys_mask,r1
    and r1,r0
    mov.l uncached_mask,r1
    or r1,r0
    jmp @r0
    nop

    .align 4

stack_addr:
    # Location of stack
    .long 0x0E000000

    .align 4

setup_cache:
    # Enable cache, copying the setup that Mvc2 does.
    mov.l ccr_addr,r0
    mov.w ccr_enable,r1
    mov.l r1,@r0

    # We must execute 8 instructions before its safe to go back.
    nop
    mov.l main_addr,r0
    nop
    mov.l phys_mask,r1
    and r1,r0
    nop
    nop
    jmp @r0
    nop

    .align 4
    .globl _icache_flush_range

    # Routine to flush parts of cache.. thanks to the Linux-SH guys
    # for the algorithm. The original version of this routine was
    # taken from sh4-gdbstub/sh-stub.c.
    #
    # r4 is the start address
    # r5 is the number of bytes to flush starting at the start address
_icache_flush_range:
    mova @((flush_real-.),pc),r0
    mov.l p2_mask,r1
    or r1,r0
    jmp @r0
    nop

    .align  4

flush_real:
    # Save old SR and disable interrupts
    stc sr,r0
    mov.l r0,@-r15
    mov.l irq_disable_orbits,r1
    or r1,r0
    ldc r0,sr

    # Get ending address from count and align start address
    add r4,r5
    mov.l l1_cache_align,r0
    and r0,r4
    mov.l cache_ic_address_array,r1
    mov.l cache_ic_entry_mask,r2
    mov.l cache_ic_valid_mask,r3

flush_loop:
    # Write back operand cache
    ocbwb @r4

    # Invalidate instruction cache
    mov r4,r6
    and r2,r6
    or r1,r6
    mov r4,r7
    and r3,r7
    add #32,r4
    cmp/hs r4,r5
    bt/s flush_loop
    mov.l r7,@r6

    # Restore old SR
    mov.l @r15+,r0
    ldc r0,sr

    # Make sure we have enough instructions before returning to P1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rts
    nop

    .align 4

l1_cache_align:
    .long ~31

cache_ic_address_array:
    .long 0xf0000000

cache_ic_entry_mask:
    .long 0x1fe0

cache_ic_valid_mask:
    .long   0xfffffc00

bss_start_addr:
    # Location of .bss section we must zero
    .long _edata

bss_end_addr:
    # Location of end of ROM where we stop zeroing
    .long _end

main_addr:
    # Location of main
    .long __enter

phys_mask:
    # Mask for converting virtual to physical address.
    .long 0x0fffffff

uncached_mask:
    # Mask for converting physical to uncached addresses.
    .long 0xa0000000

p2_mask:
    # Mask for converting physical to P2 address.
    .long 0x20000000

ccr_addr:
    # Address of cache control register.
    .long 0xFF00001C

ccr_enable:
    # Value to write to cache control register to enable i-cache and d-cache.
    .long 0x00000105

ccr_icache_flush:
    # Value to write to cache control register to flush previously enabled i-cache
    # but leave previously enabled d-cache alone.
    .long 0x00000905

sr_disable:
    # Value to write to SR to disable interrupts, mask them all off and start
    # in register bank 0.
    .long 0x500000F0

    .align 4
    .globl __irq_get_sr

__irq_get_sr:
    # Just grab the SR and stick it in R0 to return it.
    stc sr,r0
    rts
    nop

    .align 4
    .globl _irq_restore

_irq_restore:
    # Load the first parameter into the SR directly. The parameter
    # given sould come from an irq_enable() or irq_disable() call.
    # This is safe to call even inside functions called from an
    # interrupt handler.
    ldc r4,sr
    rts
    nop

    .align 4
    .globl  __irq_enable

__irq_enable:
    # First, grab a SR mask which turns off all IMASK bits.
    mov.l irq_enable_andbits,r1

    # Now, store the old SR value in r0 so we can return it.
    stc sr,r0

    # Now, set the new SR value to the old value but with all IMASK bits off
    # and BL cleared so that interrupts can flow.
    and r0,r1
    ldc r1,sr

    # Finally, return the old SR value to the caller.
    rts
    nop

    .align 4

irq_enable_andbits:
    # Turn off the BL bit to unblock interrupts, clear the IMASK bits.
    .long   0xefffff0f

    .align 4
    .globl  _irq_disable

_irq_disable:
    # First, grab a SR mask which turns on all IMASK bits.
    mov.l   irq_disable_andbits,r1
    mov.l   irq_disable_orbits,r2

    # Now, store the old SR value in r0 so we can return it.
    stc sr,r0

    # Now, set the new SR value to the old value but with all IMASK bits on.
    and r0,r1
    or  r2,r1
    ldc r1,sr

    # Finally, return the old SR value to the caller.
    rts
    nop

    .align 4

irq_disable_andbits:
    # Going to explicitly set BL, so we are completely blocked from
    # any interrupts or exceptions inside our handler. Also, mask off
    # the IMASK bits.
    .long   0xefffff0f
irq_disable_orbits:
    # Add back in the mask bits to turn on all IMASK bits to disable
    # interrupts, also set BR to blocked so that exceptions don't work.
    .long   0x100000f0

    .align 4
    .globl  __irq_read_sr

__irq_read_sr:
    # Store the SR value in r0 so we can return it.
    stc sr,r0
    rts
    nop

    .align 4
    .globl  __irq_read_vbr

__irq_read_vbr:
    # Store the VBR value in r0 so we can return it.
    stc vbr,r0
    rts
    nop

    .align 4
    .global __irq_set_vector_table

__irq_set_vector_table:
    # Load the addres of the below vector table into VBR.
    mov.l vector_table_address,r0
    ldc r0,vbr

    # Return to caller.
    rts
    nop

    .align 4

vector_table_address:
    .long _irq_vector_table_base

    .align 4

_irq_base_handler:
    # First, save our register state into the global register state pointer.
    mov.l _irq_state,r0

    # Start by saving banked registers which were R0-R7 of the running process
    # before we were interrupted. Adjust the pointer to &gp_regs[8] and store
    # backwards.
    add #0x20,r0
    stc.l r7_bank,@-r0
    stc.l r6_bank,@-r0
    stc.l r5_bank,@-r0
    stc.l r4_bank,@-r0
    stc.l r3_bank,@-r0
    stc.l r2_bank,@-r0
    stc.l r1_bank,@-r0
    stc.l r0_bank,@-r0

    # Now, save the unbanked registers we were careful not to touch.
    mov.l r8,@(0x20,r0)
    mov.l r9,@(0x24,r0)
    mov.l r10,@(0x28,r0)
    mov.l r11,@(0x2c,r0)
    mov.l r12,@(0x30,r0)
    mov.l r13,@(0x34,r0)
    mov.l r14,@(0x38,r0)
    mov.l r15,@(0x3c,r0)

    # Now, we point at the beginning of _irq_state again, since we decremented
    # eight times and stored 4 bytes each in the above R0-R7 store. Adjust the
    # pointer to just below "sr" in irq_state_t so that we can start storing
    # the special purpose registers. Then, store "backwards".
    add #0x5c,r0
    stc.l ssr,@-r0
    sts.l macl,@-r0
    sts.l mach,@-r0
    stc.l vbr,@-r0
    stc.l gbr,@-r0
    sts.l pr,@-r0
    stc.l spc,@-r0

    # Now, we point at &pc in _irq_state. We want to adjust forward enough to
    # get to the end of _irq_state so when we store FPUL and FPSCR they go in
    # the right slots. This is 0xA4 forward, but we can't add that much in one
    # go since add takes an 8-bit signed immediate. Then, save all the floating
    # point registers.
    add #0x60,r0
    add #0x44,r0
    sts.l fpul,@-r0
    sts.l fpscr,@-r0
    fmov.s fr15,@-r0
    fmov.s fr14,@-r0
    fmov.s fr13,@-r0
    fmov.s fr12,@-r0
    fmov.s fr11,@-r0
    fmov.s fr10,@-r0
    fmov.s fr9,@-r0
    fmov.s fr8,@-r0
    fmov.s fr7,@-r0
    fmov.s fr6,@-r0
    fmov.s fr5,@-r0
    fmov.s fr4,@-r0
    fmov.s fr3,@-r0
    fmov.s fr2,@-r0
    fmov.s fr1,@-r0
    fmov.s fr0,@-r0

    # We can't accesss banked registers directly like we could with R0-R7 above.
    # So swap banks and keep storing.
    frchg
    fmov.s fr15,@-r0
    fmov.s fr14,@-r0
    fmov.s fr13,@-r0
    fmov.s fr12,@-r0
    fmov.s fr11,@-r0
    fmov.s fr10,@-r0
    fmov.s fr9,@-r0
    fmov.s fr8,@-r0
    fmov.s fr7,@-r0
    fmov.s fr6,@-r0
    fmov.s fr5,@-r0
    fmov.s fr4,@-r0
    fmov.s fr3,@-r0
    fmov.s fr2,@-r0
    fmov.s fr1,@-r0
    fmov.s fr0,@-r0

    # Now, swap back just for good measure. I'm not entirely sure this is necessary?
    # I guess if some interrupted code took a direct pointer to which bank of the
    # floating point unit it was on, then not doing this could cause problems?
    frchg

    # Now, set up a small stack for our own routines to use.
    mov.l _irq_stack,r15

    # Now, call the general purpose IRQ handler. R4 still contains the interrupt
    # source. Make it into an integer offset which matches the VBR table in the SH-4
    # manual.
    shll8 r4
    mov.l irq_handler_addr,r2
    jsr @r2
    nop

    # Now, restore our registers from the register state itself. Note that this
    # could have changed in the interrupt handler, but as long as the state is
    # valid (for some contexxt that was saved), it should work. In this way, we
    # can provide not just interrupts but also threads.
    mov.l _irq_state,r0

    # We can restore in a regular order now, so start at the top with the banked
    # registers R0-R7.
    ldc.l @r0+,r0_bank
    ldc.l @r0+,r1_bank
    ldc.l @r0+,r2_bank
    ldc.l @r0+,r3_bank
    ldc.l @r0+,r4_bank
    ldc.l @r0+,r5_bank
    ldc.l @r0+,r6_bank
    ldc.l @r0+,r7_bank

    # Now, restore the unbanked registers R8-R15.
    mov.l @(0x00,r0), r8
    mov.l @(0x04,r0), r9
    mov.l @(0x08,r0), r10
    mov.l @(0x0c,r0), r11
    mov.l @(0x10,r0), r12
    mov.l @(0x14,r0), r13
    mov.l @(0x18,r0), r14
    mov.l @(0x1c,r0), r15

    # Now, advance to the special registers and restore those.
    add #0x20,r0
    ldc.l @r0+,spc
    lds.l @r0+,pr
    ldc.l @r0+,gbr
    ldc.l @r0+,vbr
    lds.l @r0+,mach
    lds.l @r0+,macl
    ldc.l @r0+,ssr

    # Now, grab the banked registers for floating-point restoration.
    frchg
    fmov.s @r0+,fr0
    fmov.s @r0+,fr1
    fmov.s @r0+,fr2
    fmov.s @r0+,fr3
    fmov.s @r0+,fr4
    fmov.s @r0+,fr5
    fmov.s @r0+,fr6
    fmov.s @r0+,fr7
    fmov.s @r0+,fr8
    fmov.s @r0+,fr9
    fmov.s @r0+,fr10
    fmov.s @r0+,fr11
    fmov.s @r0+,fr12
    fmov.s @r0+,fr13
    fmov.s @r0+,fr14
    fmov.s @r0+,fr15

    # And swap back to get the non-banked registers.
    frchg
    fmov.s @r0+,fr0
    fmov.s @r0+,fr1
    fmov.s @r0+,fr2
    fmov.s @r0+,fr3
    fmov.s @r0+,fr4
    fmov.s @r0+,fr5
    fmov.s @r0+,fr6
    fmov.s @r0+,fr7
    fmov.s @r0+,fr8
    fmov.s @r0+,fr9
    fmov.s @r0+,fr10
    fmov.s @r0+,fr11
    fmov.s @r0+,fr12
    fmov.s @r0+,fr13
    fmov.s @r0+,fr14
    fmov.s @r0+,fr15

    # And finally, get the status and communication registers for floating point.
    lds.l @r0+,fpscr
    lds.l @r0+,fpul

    # Finally, return from interrupts.
    rte
    nop

    .align 4
    .globl _irq_stack

_irq_stack:
    .long 0

    .globl _irq_state

_irq_state:
    .long 0

irq_handler_addr:
    .long __irq_handler

    .align 4

_irq_vector_table_base:
    .rep    0x100
    .byte   0
    .endr

_irq_vector_table_general_exceptions:
    # Handler at 0x100 offset from the vector base. Set exception code to "1"
    # and enter interrupt vector. Its safe to write over r0-r8 here since the
    # SH-4 has swapped register banks from 0 (our init value) to 1 (interrupt).
    nop
    bra _irq_base_handler
    mov #1,r4

    .rep    0x300 - 6
    .byte   0
    .endr

_irq_vector_table_tlb_miss_exceptions:
    # Handler at 0x400 offset from the vector base. Set exception code to "4"
    # and enter interrupt vector. Its safe to write over r0-r8 here since the
    # SH-4 has swapped register banks from 0 (our init value) to 1 (interrupt).
    nop
    bra _irq_base_handler
    mov #4,r4

    .rep    0x200 - 6
    .byte   0
    .endr

_irq_vector_table_general_interrupt:
    # Handler at 0x600 offset from the vector base. Set exception code to "6"
    # and enter interrupt vector. Its safe to write over r0-r8 here since the
    # SH-4 has swapped register banks from 0 (our init value) to 1 (interrupt).
    nop
    bra _irq_base_handler
    mov #6,r4
