# r8  - Address of the maple bus command buffer (uncached).
# r9  - Maple bus DMA buffer register address (physical).
# r10 - Address of data location in this rom.
# r11 - Address of maple response buffer (uncached).
# r15 - Address of the function stack.

#define pMapleCommandBuffer r8
#define pMapleRegs r9
#define pDataOffset r10
#define pMapleResponseBuffer r11
#define pStack r15

# There are no absolute addresses here since we expect to be patched anywhere
# in the ROM that has enough space.

    .section .text
    .globl start

start:
    # First, we need to enable cache since the BIOS disables it
    # before calling into ROM space. So, get ourselves into P2
    # region so its safe to enable cache.
    mova @(16,pc),r0
    mov.l phys_mask,r1
    and r1,r0
    mov.l p2_mask,r1
    or r1,r0
    jmp @r0
    nop
    nop

# Enable cache, copying the setup that Mvc2 does.
setup_cache:
    # This is exactly 16 bytes forward of the above mova instruction.
    mov.l ccr_addr,r0
    mov.w ccr_enable,r1
    mov.l r1,@r0

    # We must execute 8 instructions before its safe to go back.
    nop
    mova @(20,pc),r0
    nop
    mov.l phys_mask,r1
    and r1,r0
    nop
    nop
    jmp @r0
    nop

ccr_enable:
    .long 0x00000105

exec_stub:
    # This is exactly 20 bytes forward of the above mova instruction.
    bsr register_setup
    nop
    # Run main program, writing an EEPROM value and then running the game.
    bra send_data
    nop

    .align 4

main_data:
    # The location where we will put our maple command/response buffers.
    .byte 0xEE
    .byte 0xEE
    .byte 0xEE
    .byte 0xEE

    # The location where we are jumping back to once we finish executing.
    .byte 0xBB
    .byte 0xBB
    .byte 0xBB
    .byte 0xBB

    # eeprom write comand header
    .byte 0x86
    .byte 0x20
    .byte 0x00
    .byte 0x05
    .byte 0x0B
    .byte 0x00
    .byte 0x10
    .byte 0x00

    # Maple bus base register
    .byte 0x00
    .byte 0x6C
    .byte 0x5F
    .byte 0xA0

return_stack:
    # Small stack we give ourselves for nested subroutines.
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00
    .byte 0x00

# Set up r8-r11 as documented at the top of the file
register_setup:
    # No alignment, this is carefully placed below the above data section so
    # the below mova instruction fines the above data.
    mova @(4,pc),r0
    add #-40,r0
    mov r0,pDataOffset

    # Locate the stack pointer for nested subroutines
    mov pDataOffset,pStack
    add #36,pStack

    # Locate the command buffer in memory, uncached
    mov pDataOffset,pMapleCommandBuffer
    mov.l @pMapleCommandBuffer,pMapleCommandBuffer
    mov.l p2_mask,r1
    or r1,pMapleCommandBuffer

    # Locate the response buffer
    mov pMapleCommandBuffer,pMapleResponseBuffer
    mov #4,r0
    shll8 r0
    add r0,pMapleResponseBuffer

    # Offset a bit more to grab the base offset for the maple bus registers
    mov pDataOffset,pMapleRegs
    add #16,pMapleRegs
    mov.l @pMapleRegs,pMapleRegs

    # I'm not sure I should be touching any of this area. The BIOS sets it up
    # and doesn't tear it down, so perhaps this is putting the maple bus in a
    # bizarre reset state on hardware, causing it to not work? I had an even
    # more complete setup (see other comments in main section below) but this
    # causes even MAME to fail to update free-play, so there's obviously some
    # other tricks I'm missing when initializing maple from scratch on Naomi

    # Initialize the maple bus with things we don't need to write again
    mov pMapleRegs,r0
    # We can't index at 0x80 or above with mov.l, so lets get a temporary location
    mov #128,r14
    extu.b r14,r14
    add r14,r0
    # Reset the hardware with sequence seen in multiple maple implementations (6155404F).
    mov.l maple_reset,r14
    mov.l r14,@(12,r0)
    # Disable hardware trigger, we will trigger using 0x10 register
    mov pMapleRegs,r0
    mov #0,r14
    mov.l r14,@(16,r0)
    # We can't index at 0x80 or above with mov.l, so lets get a temporary location
    mov #128,r14
    extu.b r14,r14
    add r14,r0
    # Set up the timing control register the same as the Naomi BIOS (2Mhz, 50000 timeout).
    mov.l maple_timeout,r14
    mov.l r14,@r0

    # We're done!
    rts
    nop

    .align 4

maple_reset:
    # Reset sequence observed in Mvc2.
    .long 0x6155404F
maple_timeout:
    # Timeout sequence observed in Mvc2.
    .long 0xC3500000

# Copy r2 32-bit chunks from source (r0) to destination (r1), clobbering r0, r1, r2 and r14
memcpy:
    mov.l @r0+,r14
    mov.l r14,@r1
    add #4,r1
    dt r2
    bf memcpy
    rts
    nop

# Memset r2 32-bit chunks from source (r0) to destination (r1), clobbering r1, r2
memset:
    mov.l r0,@r1
    add #4,r1
    dt r2
    bf memset
    rts
    nop

# Wait until maple bus DMA is finished, clobbering r0
wait_dma_ready:
    mov.l @(24,pMapleRegs),r0
    tst r0,r0
    bf wait_dma_ready

    rts
    nop

# Perform a command to maple bus hardware, clobbering r0
# Assumes that pMapleCommandBuffer points at a valid request and
# pMapleResponseBuffer is a valid location for the response.
exchange_maple:
    # First, save our return pointer so we can call subroutines ourselves
    sts.l PR,@-pStack

    # Mask off the upper bits of our address/response so that it is always physical.
    mov.l phys_mask,r0
    and pMapleResponseBuffer,r0
    # Update the command buffer with the response buffer so we can receive responses.
    mov.l r0,@(4,pMapleCommandBuffer)

    # Set the maple bus device enabled register.
    mov #1,r0
    mov.l r0,@(20,pMapleRegs)
    # Set the DMA pointer register to our physical address.
    mov.l phys_mask,r0
    and pMapleCommandBuffer,r0
    mov.l r0,@(4,pMapleRegs)
    # Set the maple bus DMA start transfer register to kick off the write.
    mov #1,r0
    mov.l r0,@(24,pMapleRegs)

    # Spin until DMA is finished (DMA start transfer register will go from 1 back to 0)
    bsr wait_dma_ready
    nop

    # Disable the maple hardware now that we transferred (TODO: Is this necessary, BIOS does this?)
    mov #0,r0
    mov.l r0,@(20,pMapleRegs)

    # Finally, return to the calling function
    lds.l @pStack+,PR
    rts
    nop

# Send a maple status request to the maple bus, wait until it claims we're ready, clobbers r0, r13, r14
wait_until_ready:
    # First, save our return pointer so we can call subroutines ourselves
    sts.l PR,@-pStack

    # Now, wait until its safe to do a DMA and mess with buffers
    bsr wait_dma_ready
    nop

    # I've noticed that actual games loop through far more infrequently than my code does
    # so possibly I need to insert a spin delay here or something to not overwhelm the maple bus?

ready_loop:
    # Set up status request
    mov #128,r14
    shll16 r14
    shll8 r14
    mov.l r14,@pMapleCommandBuffer
    mov #32,r13
    shll8 r13
    mov #1,r14
    or r14,r13
    mov.l r13,@(8,pMapleCommandBuffer)

    # Update the response buffer with 0xFFFFFFFF so we can get the correct response
    mov #255,r14
    mov.l r14,@(0,pMapleResponseBuffer)

    # Perform a maple bus write
    bsr exchange_maple
    nop

    # Check to see if we are ready (No timeout, no non-response)
    mov.b @pMapleResponseBuffer,r14
    mov #252,r13
    cmp/eq r13,r14
    bt ready_loop

    # Finally, return to the calling function
    lds.l @pStack+,PR
    rts
    nop

# Request to write 16 bytes at offset r0 to the EEPRom, assumes the 16 bytes following
# command header are filled in by calling function. Clobbers r0, r13, r14
write_eeprom:
    # First, save our return pointer so we can call subroutines ourselves
    sts.l PR,@-pStack

    # Save our offset since wait_dma_ready clobbers r0
    mov r0,r14
    shll8 r14

    # Now, wait until its safe to do a DMA and mess with buffers
    bsr wait_dma_ready
    nop

    # Set up EEPRom write request
    mov #128,r0
    shll16 r0
    shll8 r0
    add #5,r0
    mov.l r0,@pMapleCommandBuffer
    mov pDataOffset,r0
    add #8,r0
    mov.l @r0,r13
    mov.l r13,@(8,pMapleCommandBuffer)
    add #4,r0
    mov.l @r0,r13
    or r14,r13
    mov.l r13,@(12,pMapleCommandBuffer)

    # Perform a maple bus write
    bsr exchange_maple
    nop

    # Finally, return to the calling function
    lds.l @pStack+,PR
    rts
    nop

    .align 4

# Copy the original game's bytes over the springboard, jump there and execute
unpatch_and_exit:
    # First, get back to P2 area so we can flush and disable cache
    mova @(16,pc),r0
    mov.l phys_mask,r1
    and r1,r0
    mov.l p2_mask,r1
    or r1,r0
    jmp @r0
    nop
    nop

    # Now, actually flush and disable cache
    mov.l ccr_addr,r0
    mov.w ccr_flush_and_disable,r1
    mov.l r1,@r0
    # We must execute 8 instructions before its safe to go back.
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop

    # Get address of where we need to jump back
    mov pDataOffset,r1
    add #4,r1
    mov.l @r1,r1
    mov r1,r6

    # Copy data back to start so our springboard is gone
    mov #3,r2
    mova @(12,pc),r0
    bsr memcpy
    nop

    # Jump back to our original entrypoint now that we're done
    jmp @r6
    nop

    .align 4

original_bytes:
    # Dummy space for the original 12 bytes that the springboard overwrites,
    # we will copy it back before jumping to it so that it executes correctly.
    .byte 0xCC
    .byte 0xCC
    .byte 0xCC
    .byte 0xCC
    .byte 0xCC
    .byte 0xCC
    .byte 0xCC
    .byte 0xCC
    .byte 0xCC
    .byte 0xCC
    .byte 0xCC
    .byte 0xCC

phys_mask:
    # Mask for converting virtual to physical address.
    .long 0x0fffffff
p2_mask:
    # Mask for converting physical to uncached addresses.
    .long 0xa0000000
ccr_addr:
    .long 0xFF00001C
ccr_flush_and_disable:
    .long 0x00000808
eeprom_data:
    # Dummy space for the EEPRom data that we will want to store. Note that
    # we intentionally write 0xFF to every location we don't know how to write,
    # which has the effect of forcing the game to re-init every play. We could
    # fix this by loaing the EEPRom contents, patching what we want in, and
    # saving it, but that's more work than I want to put in and I want to be
    # able to change settings over the wire instead of the service menu.
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA
    .byte 0xAA

# Send our custom stub to the EEPRom so the game finds it and uses it for settings
send_data:
    # Originally we had a wait until reset command here (command 0x03) and on real HW this is
    # issued first. However, when we do this, MAME fails to be in free-play mode and I'm
    # pretty sure its because the MIE itself, once reset, needs more than what we give it to
    # unlock the EEPROM again. So I just make the assumption that the BIOS left it unlocked.

    # Wait until its ready for commands
    bsr wait_until_ready
    nop

    # Set r5 to the top of the above data chunk, so we can copy it out in 16 byte chunks
    mova @(8,pc),r0
    mov r0,r5
    add #-60,r5
    # We have three 16-byte chunks to write
    mov #3,r6
    # We start writing to offset 0
    mov #0,r7

patch_loop:
    # Copy chunk of data to be written
    mov r5,r0
    mov pMapleCommandBuffer,r1
    add #16,r1
    mov #4,r2
    bsr memcpy
    add #16,r5

    # Set up and copy those bytes to eeprom
    mov r7,r0
    bsr write_eeprom
    add #16,r7

    # Delay until we are ready for the next write
    bsr wait_until_ready
    nop
    dt r6
    bf patch_loop

    # We have five 16-byte chunks of 0xff... to write
    mov #5,r6

wipe_loop:
    # Wipe chunk of data to be written
    mov #255,r0
    mov pMapleCommandBuffer,r1
    add #16,r1
    bsr memset
    mov #4,r2

    # Set up and copy those bytes to eeprom
    mov r7,r0
    bsr write_eeprom
    add #16,r7

    # Delay so write can occur
    bsr wait_until_ready
    nop
    dt r6
    bf wipe_loop

    # Done, lets get back to the game!
    bra unpatch_and_exit
    nop


# TODO: All of this crap below is debugging and should go
# Takes a hex value 0-F in r1, and "prints" it to the address in r0, clobbers r1, r12, increments r0
.align 4
print_hex_digit:
mov #9,r12
cmp/ge r1,r12
bf letter
add #48,r1
mov.b r1,@r0
rts
add #1,r0
letter:
add #55,r1
mov.b r1,@r0
rts
add #1,r0

# Crap to align below
nop

.byte 0x60
.byte 0x74
.byte 0x22
.byte 0xAC

debug_crap:
# Load the above address for a string we can fuck with
mova @(8,pc),r0
add #-12,r0
mov.l @r0,r0

# Print out maple request buffer
mov pMapleResponseBuffer,r3
mov #4,r2
bra print_memory
nop

print_memory:
mov.b @r3,r14
extu.b r14,r14
mov r14,r1
shlr2 r1
shlr2 r1
bsr print_hex_digit
mov #15,r13
and r13,r1

mov r14,r1
bsr print_hex_digit
and r13,r1
add #1,r3
dt r2
bf print_memory

# End the line
mov #10,r1
mov.b r1,@r0
add #1,r0
mov #0,r1
mov.b r1,@r0

bra unpatch_and_exit
nop
