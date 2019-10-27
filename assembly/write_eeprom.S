# r8  - Address of the maple bus command/response buffer.
# r9  - Maple bus DMA buffer register address.
# r10 - Address of eeprom write buffer.

#define pMapleBuffer r8
#define pMapleRegs r9
#define pMapleCmdBuf r10

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

# Mask for converting virtual to physical address.
.byte 0xFF
.byte 0xFF
.byte 0xFF
.byte 0x0F

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

# Maple transfer descriptor, will be filled in at runtime, pMapleBuffer points here
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
# Grab the current PC and offset it to get the maple bus command buffer
mova @(8,pc),r0
mov r0,pMapleBuffer
add #-40,pMapleBuffer
# Set up the eeprom write buffer
mov pMapleBuffer,pMapleCmdBuf
add #16,pMapleCmdBuf

# Offset a bit more to grab the base offset for the maple bus registers
mov pMapleBuffer,pMapleRegs
add #-4,pMapleRegs
mov.l @pMapleRegs,pMapleRegs
# Initialize the maple bus with things we don't need to write again
mov pMapleRegs,r0
# Disable maple access while initializing
mov #0,r15
mov.l r15,@(20,r0)
# We can't index at 0x80 or above with mov.l, so lets get a temporary location
mov #128,r15
extu.b r15,r15
add r15,r0
# Reset the hardware with sequence seen in multiple maple implementations.
mov #97,r15
shll8 r15
shll16 r15
mov #85,r14
shll16 r14
or r14,r15
mov #64,r14
shll8 r14
or r14,r15
mov #116,r14
or r14,r15
mov.l r15,@(12,r0)
# Set up the timing control register the same as the Naomi BIOS
mov #58,r15
shll8 r15
mov #152,r14
extu.b r14,r14
or r14,r15
shll16 r15
mov.l r15,@r0
# Disable hardware trigger, we will trigger using 0x10 register
mov pMapleRegs,r0
mov #0,r15
mov.l r15,@(16,r0)
rts
nop

# Write 16 bytes that are left in the EEPRom buffer to the EEPRom offset
# pointed at by r0.
write_eeprom:
# Copy the eeprom write command headers to the maple buffer
mov pMapleBuffer,r15
add #-16,r15
mov.l @r15,r14
mov.l r14,@pMapleBuffer
mov #5,r14
mov.b r14,@pMapleBuffer
add #4,r15
mov.l @r15,r14
mov.l r14,@(8,pMapleBuffer)
add #4,r15
mov.l @r15,r14
mov.l r14,@(12,pMapleBuffer)
# Update the command with the write offset
mov.b r0,@(13,pMapleBuffer)
# Mask off the upper bits of our address/response so that it is always physical.
mov pMapleBuffer,r11
add #-24,r11
mov.l @r11,r11
and pMapleBuffer,r11
# Update the command buffer with its own location for receiving the response.
mov.l r11,@(4,pMapleBuffer)

# Prepare to kick off an actual maple bus write to trigger eeprom update.
mov pMapleRegs,r0
mov #1,r15

# Wait until we can safely start transferring data (any previous DMA is done)
wait_spin:
mov.l @(24,r0),r14
tst r15,r14
bf wait_spin

# Set the DMA pointer register to our physical address.
mov.l r11,@(4,r0)
# Set the maple bus device enabled register.
mov.l r15,@(20,r0)
# Set the maple bus DMA start transfer register to kick off the write.
mov.l r15,@(24,r0)

# Spin until DMA is finished (DMA start transfer register will go from 1 back to 0)
write_spin:
mov.l @(24,r0),r14
tst r15,r14
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
# Do two complete passes like Naomi bios does.
mov #2,r4
nop
send_loop:
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
mov pMapleCmdBuf,r1
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
mov pMapleCmdBuf,r1
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
dt r4
bf send_loop

# Get address of where we need to jump back
mov pMapleBuffer,r6
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
