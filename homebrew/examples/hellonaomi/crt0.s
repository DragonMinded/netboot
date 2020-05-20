    .section .text
    .globl start

start:
    # First, we need to initialize the stack (we don't have a frame pointer)
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
    mova @(16,pc),r0
    mov.l phys_mask,r1
    and r1,r0
    mov.l p2_mask,r1
    or r1,r0
    jmp @r0

    # These are only here to make sure setup_cache is exactly 16
    # bytes (8 instructions) forward of the indirect pc load above.
    nop
    nop

    # This is exactly 16 bytes forward of the above mova instruction.

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

    # TODO: At some point we need to call global ctors/dtors, init and fini,
    # but for now that's left out.

stack_addr:
    # Location of stack
    .long 0x0E000000

bss_start_addr:
    # Location of .bss section we must zero
    .long _edata

bss_end_addr:
    # Location of end of ROM where we stop zeroing
    .long _end

main_addr:
    # Location of main
    .long _main

phys_mask:
    # Mask for converting virtual to physical address.
    .long 0x0fffffff

p2_mask:
    # Mask for converting physical to uncached addresses.
    .long 0xa0000000

ccr_addr:
    # Address of cache control register.
    .long 0xFF00001C

ccr_enable:
    # Value to write to cache control register to enable i-cache and d-cache.
    .long 0x00000105
