# r8  - Address of the maple bus command/response buffer.
# r9  - Maple bus DMA buffer register address.
# r10 - Maple bus device enable register address.
# r11 - Maple bus DMA start transfer/status register address.
# r12 - Address of eeprom write buffer.

.section .text
.globl start

start:
# Set up registers we care about
bsr register_setup
# Branch delay slot
nop
# Run main program
bra send_data
# Branch delay slot
nop

# Dummy bytes for alignment purposes
.byte 0x00
.byte 0x00
.byte 0x00
.byte 0x00

# The location where we are jumping back to once we finish executing.
.byte 0xBB
.byte 0xBB
.byte 0xBB
.byte 0xBB

# Transfer descriptor header
.byte 0x00
.byte 0x00
.byte 0x00
.byte 0x80

# eeprom write comand header
.byte 0x86
.byte 0x02
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
.byte 0x00

# Maple transfer descriptor, will be filled in at runtime, r8 points here
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF

# Dummy response location that will be filled in at runtime
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF

# Buffer that will be updated for actual commands
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0xFF

register_setup:
# Grab the current PC and offset it to get the maple bus command buffer, holding it in R8
mova @(8,pc),r0
mov r0,r8
add #-40,r8
# Set up the eeprom write buffer
mov r8,r12
add #16,r12

# Offset a bit more to grab the base offset for the maple bus registers, holding that in R9
mov r8,r9
add #-4,r9
mov.l @r9,r9
# Update R10 to be the DMA enable register
mov r9,r10
add #20,r10
# Update R11 to be the DMA start transfer register
mov r9,r11
add #24,r11
# Update R9 to be the DMA source register
add #4,r9
rts
nop

# Write 16 bytes that are left in the EEPRom buffer to the EEPRom offset
# pointed at by r0.
write_eeprom:
# Copy the eeprom write command headers to the maple buffer
mov r8,r15
add #-16,r15
mov.l @r15,r14
mov.l r14,@r8
mov #5,r14
mov.b r14,@r8
add #4,r15
mov.l @r15,r14
mov.l r14,@(8,r8)
add #4,r15
mov.l @r15,r14
mov.l r14,@(12,r8)
# Update the command with the write offset
mov.b r0,@(13,r8)
# Update the command buffer with its own location for receiving the response.
mov.l r8,@(4,r8)

# Start the copy by writing the correct values to the correct registers
mov.l r8,@r9
mov #1,r15
mov.l r15,@r10
mov.l r15,@r11

# Spin until DMA is finished (DMA start transfer register will go from 1 back to 0)
write_spin:
mov #1,r14
mov.l @r11,r15
tst r14,r15
bf write_spin
rts
nop

# Copy r2 32-bit chunks from source (r0) to destination (r1)
memcpy:
mov.l @r0+,r15
mov.l r15,@r1
add #4,r1
dt r2
bf memcpy
rts
nop

# Memset r2 32-bit chunks from source (r0) to destination (r1)
memset:
mov.l r0,@r1
add #4,r1
dt r2
bf memset
rts
nop

# Spin for r0 cycles
delay:
dt r0
bf delay
rts
nop

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

send_data:
# Set r5 to the top of the above data chunk, so we can copy it out in 16 byte chunks
mova @(8,pc),r0
mov r0,r5
add #-56,r5
# We have three 16-byte chunks to write
mov #3,r6
# We start writing to offset 0
mov #0,r7

patch_loop:
# Copy chunk of data to be written
mov r5,r0
mov r12,r1
mov #4,r2
bsr memcpy
add #16,r5
# Set up and copy those bytes to eeprom
mov r7,r0
bsr write_eeprom
add #16,r7
# Delay so write can occur
mov #127,r0
bsr delay
shll16 r0
dt r6
bf patch_loop

# We have five 16-byte chunks of 0xff... to write
mov #5,r6

wipe_loop:
# Wipe chunk of data to be written
mov #255,r0
mov r12,r1
bsr memset
mov #4,r2
# Set up and copy those bytes to eeprom
mov r7,r0
bsr write_eeprom
add #16,r7
# Delay so write can occur
mov #127,r0
bsr delay
shll16 r0
dt r6
bf wipe_loop

# Get address of where we need to jump back
mov r8,r6
add #-20,r6
mov.l @r6,r6

# Copy data back to start so our springboard is gone
add #-60,r5
mov r5,r0
mov r6,r1
bsr memcpy
mov #3,r2

# Jump back to our original entrypoint now that we're done
jmp @r6
nop
