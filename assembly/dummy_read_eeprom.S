# r0 - Response location that game moves to r5, the game does not use this after moving.
# r3 - Spare register (the game uses this for unrolled memcpy).
# r4 - Amount of data the game will copy (should be 0x80, we use this and restore it).
# r5 - EEPRom response inside maple response buffer that the game sets up by adding 4 to r0.
# r6 - Spare register (the game overwrites this).
# r7 - Destination for the memcpy of 0x80 that we need to also perform.

#define pDataOffset r3
#define pEEPROMLocation r5
#define pDestinationLocation r7

# There are no absolute addresses here since we expect to be patched anywhere
# in the ROM that has enough space.

    .section .text
    .globl start

start:
    # Set up to grab the below data
    # Run main program, writing an EEPROM value and then running the game.
    bra register_setup
    nop

run_patch:
    # Copy the bytes from eeprom_data below into the maple response buffer.
    # Loop is unrolled to clobber minimum number of registers.
    mov pDataOffset,r6
    add #4,r6 

    # Copy the EEPROM settings that we generated over to the memcpy response
    # buffer. Advance both the EEPROM pointer and our destination pointer so
    # we can copy the rest of the read buffer later.
    mov #36,r4
copy_loop:
    mov.b @r6+,r0
    add #-1,r4
    tst r4,r4
    mov.b r0,@pDestinationLocation
    add #1,pDestinationLocation
    bf/s copy_loop
    add #1,pEEPROMLocation

    # Set up to do the rest of the memcpy that we clobbered
    mov #92,r4
    
memcpy_updated_data:
    # Now, we need to do the memcpy that we clobbered.
    mov.b @pEEPROMLocation+,r0
    add #-1,r4
    tst r4,r4
    mov.b r0,@pDestinationLocation
    bf/s memcpy_updated_data
    add #1,pDestinationLocation

fix_registers_and_exit:
    # Restore r0 now that we're done memcpying. That's 128
    # back (0x80 memcpy'd above) plus 4 to skip the original
    # maple response buffer.
    mov pEEPROMLocation,r0
    add #-64,r0
    add #-68,r0

    # Restore r7 as well now that we're done memcpying. That's
    # 128 back.
    add #-64,pDestinationLocation
    add #-64,pDestinationLocation

    # Get address of where we need to jump back.
    mov.l @pDataOffset,r6

    # Now jump back after the memcpy in the original game.
    jmp @r6
    nop

    .align 4

register_setup:
    # This is carefully placed above the below data so the mova can locate
    # our stuff. We do this since we don't know the absolute address of where
    # we are running, so we use the PC instead.
    mova @(8,pc),r0
    mov r0,pDataOffset
    bra run_patch
    nop

main_data:
    # The location where we are jumping back to once we finish executing.
    .byte 0xBB
    .byte 0xBB
    .byte 0xBB
    .byte 0xBB

eeprom_data:
    # Dummy space for the EEPRom data that we will want to dummy-load. We copy
    # this into the maple response buffer, so we only need to store the exact
    # bytes we're clobbering.
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
