# Naomi Reverse Engineering Documentation

I attempted to write a universal EEPROM write patch for Naomi in order to allow a controlling host program such as a NetDimm Send application to set the system settings. While I succeeded in executing code on Naomi hardware, I was unsuccessful in talking with the maple bus to write to EEPROM and thus gave up that avenue. However, I ended up RE'ing a bunch of stuff which might be useful for people homebrewing for Naomi in the future which is documented here.

## Main Executable Layout

The Naomi BIOS will load the rom header (first 1000 bytes) through the simulated GD-ROM interface in order to determine how to load the main executable. Then, depending on some parameters in this header, it will load the main game executable and then jump to the entrypoint, starting the game. Even though the Naomi has ROM cartridges, it does not access them through memory mapped interface. Instead, it uses GD-ROM registers much like the Dreamcast. So, parts of the rom must be DMA'd over using the GD-ROM interface hardware before executing. This means that if you are using a Dimm board with a GD-ROM attached, it has its own firmware to load the image off of the GD-ROM itself and into its memory, which is then DMA'd over by the Naomi BIOS itself. Bit of a rube goldberg machine but it works.

The header of a Naomi rom is as follows. I have only documented locations that I've RE'd or figured out by observation, the rest is a mystery to me. This is enough to use an existing game and replace the main executable to get homebrew on the system, or to understand where to search in the ROM for making hacks or fixes.

* `0x000` = 16 bytes ASCII system description, always set to `NAOMI           ` in practice (yes, with 0x20 as pad bytes).
* `0x010` = 32 byte space-padded ASCII string of publisher/copyright holder.
* `0x030` = 32 byte space-padded ASCII string of game title in Japan region.
* `0x050` = 32 byte space-padded ASCII string of game title in USA region.
* `0x070` = 32 byte space-padded ASCII string of game title in Export region.
* `0x090` = 32 byte space-padded ASCII string of game title in Korea region.
* `0x0B0` = 32 byte space-padded ASCII string of game title in Australia region.

There are three more unused region strings after this, spaced exactly where you expect them. Some games leave this empty. Some forgot to change it and it has a dummy starter game name. Some games set this to a generic title without region info.

* `0x134` = 4 byte ASCII serial number (used by BIOS for validating system settings, see EEPROM below).
* `0x138` = Unknown flag that controls something to do with main ROM copy. Observed to be `0x01` and `0x00` in practice.
* `0x360` = 4 byte offset into ROM file where BIOS should start copying for the main executable.
* `0x364` = Load address in main memory where copy should go. The first byte found at the offset specified at `0x360` will be placed into the memory region specified at `0x364`, and the rest of the bytes follow.
* `0x368` = 4 byte offset into ROM file where BIOS should stop copying. The length of the main executable is therefore this value minus the start offset above.
* `0x420` = entrypoint to jump to for main game after copy is done. This will always be at or after the load address above, but before load address plus the length.
* `0x3C0` = 4 byte offset into ROM file for test mode executable. Identical to above documentation, but what's loaded for test.
* `0x3C4` = Load address in main memory where copy should go, identical to above documentation.
* `0x3C8` = 4 byte offset into the ROM file to stop copying the test executable. Identical to above.
* `0x424` = Entrypoin in main RAM to jump to for test mode after copy is done.

Games are free to put whatever values in they please, and they absolutely do. For instance, MvsC2 specifies an offset of `0x1000` and a load address `0x0c021000` and an entrypoint of `0x0c021000` which means the BIOS copies from the offset `0x1000` in the ROM to memory location `0x0c021000` before jumping to `0x0c021000` to start executing. Ikaruga decides that the offset is `0x0` which means that the main executable copy includes the header. They set the load address to `0x8c020000` but set the entrypoint to `0x8c021000`, or `0x1000` bytes into the copy. Presumably they could have set the offset to `0x1000` like MvsC2 and adjusted the load address accordingly to skip loading the first `0x1000` bytes, but it works out the same. Monkey Ball pulls some shenanigans where they set the offset for both the main executable and test executable to the same spot in the ROM, and loading to the same spot in main RAM. They then change the entrypoint to be two instructions different for test mode versus the main executable. If you look at the instructions that are pointed to by the two entrypoints, it sets a register to have one of two values. Presumably, somewhere in the executable is a check to ask whether it is test mode or normal mode based on the register. Somewhat clever if you ask me.

Most games have a fairly common startup: They jump to uncached mirror, enable cache using the CCR, then memset their working ram to 0x00, initialize FPU registers and other stuff, and then get started loading assets and reading EEPROM.

## Compiling Code for Hacks/Homebrew/Fixes

The Naomi is similar enough to the Dreamcast that much of the hardware is the same. There is a standard GCC toolchain available which generates code which works on actual Naomi. I didn't do the work but I am willing to bet that porting the Dreamcast version of KallistoOS would be fairly trivial and then you would be able to write your own games/greetz/trainers/etc.

The SH-4 toolchain for GCC is still maintained, so you can install it on a Linux system such as Debian using `apt`. Search for the SH-4 GCC toolchain on your favorite distro and then simply install it. No compiling needed. On my debian system I was able to compile SH-4 assembly to raw bytes that could be patched onto game executables like the following. Note that I used relative instructions only since I take the raw opcodes and never specify to the linker where my start address is. If you go further than me and specify the start address (basically set it to the game header you are using as a template's entrypoint) then you should be able to use absolute jumps and compile C/C++ code just fine.

For example, to compile a simple assembly file that I can patch onto MvsC2, I did the following:

```
# First, run the preprocessor over the .S file so that I can use #define to alias registers
sh4-linux-gnu-gcc -E some_file.S -o some_file.s

# Now, take the preprocessed .s file from above step, compile it to an intermediate .o file.
sh4-linux-gnu-as --isa sh4 -o some_file.o some_file.s

# Now, instead of linking, just use ld to give me the raw bytes (hence relative-only instructions).
sh4-linux-gnu-ld -e start --oformat binary -o some_file.bin some_file.o
```

If I do the above, I end up with `some_file.bin` which are raw bytes that can be pasted wherever you please and will be executed by the Naomi when the PC gets to that point. An example .S file where I was writing a springboard looks like the following. The `.globl start` is how I am able to reference the section in the above command, in order to extract the raw opcodes directly:

```
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
```

Doing anything useful with this is left as an exercise to the reader, but with all of the above and the tools located in this repo it should be fairly easy for an enterprising young hacker to get started patching their favorite games.

## System Memory Layout

The Naomi is almost identical to the Dreamcast, with a similar SH-4 processor. GD-ROM games use the MMU, and its possible that cartridge games do as well. Most games enable i-cache and op-cache and execute out of the cached region of main memory. Uncached access is managed through the standard SH-4 mirror at 0xA0000000. So, if you are accessing 0x1234 and want to access the raw memory without cache (say to poke at a HW register), you would access 0xA0001234. I won't elaborate on this as you can read the SH-4 processor spec to learn more. As of this writing, it is located at [https://www.renesas.com/us/en/doc/products/mpumcu/001/rej09b0318_sh_4sm.pdf](https://www.renesas.com/us/en/doc/products/mpumcu/001/rej09b0318_sh_4sm.pdf).

The SH-4 documents that the reset vector points at 0xA0000000 which is the uncached mirror of 0x00000000. The BIOS is located here, and given that the system starts executing with cache disabled, it is safe in this region for the BIOS to enable cache. Game SRAM is available at 0x200000 and is 0x8000 in size. This appears to be used by games for high-score tables, as game settings and unlocks are stored in the EEPROM. The main RAM is at 0xC000000 and is 0x2000000 in size. This is where the BIOS moves the executable to and jumps to, and games are responsible for talking to the GD-ROM hardware to load assets into RAM when needed.

## EEPROM Access

The EEPROM used for system and game settings is off the MIE chip in the same physical location as the DIP switches. It is accessed using maple bus commands using an undocumented maple command/response. The maple hardware in the Dreamcast is identical to the maple hardware in Naomi, so you can refer to one of various web sites which discuss Dreamcast maple programming for valid ways to talk to it. For completeness, the registers are listed here at their physical address. Remember to access them using the uncached mirror or you will be a sad panda. To drive the point home, addresses below are listed in their uncached region.

* `0xA05F6C04` = DMA buffer in main memory, where transfer descriptors go. Must be 32-byte aligned and physical.
* `0xA05F6C10` = Hardware trigger to start DMA transfer, seemingly unused for most of Naomi. Initialize with `0`and leave alone.
* `0xA05F6C14` = Maple device enable. If set to `0`, Start DMA transfer below won't trigger. Set to `1` to enable the hardware.
* `0xA05F6C18` = Start DMA transfer. Write a `1` to start a DMA after giving a valid transfer descriptor at the address placed into the DMA buffer register above. Read to verify DMA is finished (will go back to `0` when response is available).
* `0xA05F6C80` = Timeout and speed control register, for waiting for maple bus operations. The Naomi BIOS sets value `0xC3500000` after hitting the init HW register below.
* `0xA05F6C8C` = Init HW register. The Naomi BIOS sets value `0x61554074` and games seem to set `0x6155404f` which matches Dreamcast magic value documented elsewhere.

In general, you will want to initialize by writing the magic value to the Init HW register, then init the Timeout register, then enable the maple device. When you want to make a transfer, write to the DMA buffer where your command is found, then write to DMA start, loop until it goes back to `0` and then read the response. For more details, check out [http://mc.pp.se/dc/maplebus.html](http://mc.pp.se/dc/maplebus.html). The MIE is maple device `0x20` which makes sense as there are only two supported controller interfaces. Make sure that the response buffer you give in your maple transfer descriptors is physical as well, but make sure that you access these buffers in memory for both creation and readin using their uncached mirrors.

The maple command for reading/writing to the MIE EEPROM is `0x86`. A valid response is `0x87`. The game seems to also ask the MIE for its version using `0x82` command, but I haven't documented that as I was unconcerned with it. The `0x86` command allows you to request a full EEPROM read or write a chunk of data. Its recommended that you write in chunks smaller than the full EEPROM so that you can emulate atomicity. As documented below, there are two copies of all data in the EEPROM for just this reason. The BIOS and games both choose to write chunks of `0x10` bytes.

## Reading EEPROM

Send an `0x86` command with the subcommand `0x01` to schedule a read. The MIE will respond with a `0x87` response, subresponse `0x02` to say that it is reading. Then, loop while sending a normal status request (`0x01` command) until the bottom byte in the response is `0xFD`. While its still working, the bottom byte in the response will be `0xFC`. Once the MIE reports that it is done, issue a `0x03` subcommand to request the entire EEPROM. The response will be 128 bytes containing the entire EEPROM contents. So, for example:

Send the following to the MIE:
* `80000001XXXXXXXX0100208600000001` - Transfer descriptor with end of transfer bit set, one byte of data payload, with response to `0xXXXXXXXX` in memory. Send to address `0x20` (MIE) the command `0x86` subcommand `0x01` to start a read. The MIE will response with `0220008700000002` to acknowlege the read request.

Send the following to the MIE:
* `80000000XXXXXXXX00002001` - Transfer descriptor with end of transfer bit set, zero bytes of data payload, with response to `0xXXXXXXXX` in memory. Send to address `0x20` the command `0x01` which is a standard status request. The MIE will respond with `002000FC` if it is still working or `002000FD` if it is done.

Send the following to the MIE:
* `80000000XXXXXXXX0100208600000003` -  Transfer descriptor with end of transfer bit set, one byte of data payload, with response to
`0xXXXXXXXX` in memory. Send to address `0x20` (MIE) the command `0x86` subcommand `0x03` to grab the previously read data. The MIE will respond with `20200087...` where `...` is 32 bytes of data (the whole 128-bit EEPROM).

## Writing EEPROM

Send an `0x86` command with the subcommand `0x0B` to schedule a write. The `0x0B` subcommand takes a few parameters: the number of bytes to write (always observed to be `0x10`) and the offset to write those bytes (always observed to be aligned to `0x10` boundary).The following bytes will be the actual bytes to write to the EEPROM. The response to the read request will be an `0x87` with, strangely, the first four bytes you asked to write in the request (probably a bug). Then, much like a read, send a normal status request (`0x01` command) and loop until the bottom byte becomes `0xFD` at which point the write will be finished. For example:

Send the following to the MIE:
* `80000005XXXXXXXX050020860010200BAAAAAAAABBBBBBBBCCCCCCCCDDDDDDDD` - Transfer descriptor with end of transfer bit set, five bytes of data payload, with response to `0xXXXXXXXX` in memory. Send to address `0x20` (MIE) the command `0x86` subcommand `0x0B` to start a write. Write `0x10` bytes of data to EEPROM offset `0x20`. Value `AAAAAAAABBBBBBBBCCCCCCCCDDDDDDDD` will be written to the specified offset. The MIE will respond with `01200087AAAAAAAA` to acknowledge the write request.

Send the following to the MIE:
* `80000000XXXXXXXX00002001` - Transfer descriptor with end of transfer bit set, zero bytes of data payload, with response to `0xXXXXXXXX` in memory. Send to address `0x20` the command `0x01` which is a standard status request. The MIE will respond with `002000FC` if it is still working or `002000FD` if it is done.

## EEPROM Layout

The MIE EEPROM stores both system and game settings. Anything you find in the test menu ends up stored here, both in system and game settings. High scores are not saved here, but unlocks such as characters in MvsC2 are stored here. Various games will stamp their 4-character serial number for both the system settings and game settings, and the Naomi uses this as a signal to wipe and re-init the various locations when you switch ROMs. This is why when you switch games it always resets coin assignments. Note that if multiple games used the same serial number, the BIOS does not actually wipe system settings. This can be tested by changing the serial number for two or more games to the same code (such as `0001`) and then loading them in succession in demul or nulldc. You'll notice that while the game settings resets (the game handles this so it sees that its magic values are wrong and re-inits), the system settings do not reset (free-play is persisted for instance). This trick however does not work on real hardware when net-booting because while the system is loading or verifying memory, it is running its own firmware and thus has its own wipe routines that it re-inits memory with.

The EEPROM is broken out into two sections: the system settings and the game settings. Each section has two identical copies of the same data, CRC protected so that the Naomi may recover from power loss mid-write. The sections are documented here:

* 36 bytes of system settings. This is the same 18-byte section duplicated so that there are two identical copies. The 18 bytes are as follows:
  * 2 bytes CRC over the 16 following bytes. This is basically CRC-16-CCITT with starting polynomial of `0xDEBDEB00` and an extra round applied over a trailing `0x00` byte that doesn't appear in the data. I won't document the whole algorithm here as I've duplicated it with some python code under `naomi/eeprom.py`. This has been verified correct across several games and system settings gathered from nulldc, demul, mame and in one instance an EEPROM dump of my own de-soldered MIE EEPROM after writing settings in the test menu on actual hardware.
  * 1 byte system attract sound setting. `0x10` = advertise on, `0x00` = advertise off. This affects the Naomi system chime and in-game attract mode. The upper 4 bits are for attract, and the lower 4 bits are for some other setting that I did not RE.
  * 4 bytes game serial, as found in the game's header at `0x134`.
  * 11 bytes additional settings. I did not RE this entire structure, but it would be trivial to twiddle various settings in test mode in demul or nulldc and observing which bytes change. The third byte is coin assignments when the chute type is set to common. It is zero-indexed, so coin assignment #1 is mapped to `0x00` and coin assignment #27 (free-play) is mapped to `0x1A`.

After the 36 bytes of system settings is the game settings. Game settings are stored slightly differently. There is a header which appears twice, followed by the data itself which appears twice. the header is documented here:

 * 2 bytes CRC over the game settings, but not the length. This is identical to the above CRC.
 * 1 byte length of the settings.
 * 1 byte copy of the length of the settings.

After two identical copies of the above header, the game's settings blob appears twice. So if the length was set to 20 bytes, you would expect to find 40 bytes of game settings data after the 8 bytes of duplicated header. Those bytes would be the same thing duplicated twice. There is no documentation as to what these bytes do as its up to the game to store. Different games have different data as well as different lengths. Many games store their game serial in the first 4 bytes, or some mangled version of it.

## Making Force System Settings Hacks

The idea is basically identical for all games. They seem to all use the same standard library compiled into the main executable which goes something like this: Load the existing EEPROM using above-described maple commands. Copy the response somewhere, check the CRC and see if the system settings need to be wiped and defaulted. This can be for one of two reasons: The CRC is bad on both copies, or the 4 byte serial doesn't match the current game. Update the system settings with defaults if it needs to happen, or accept the current settings if they match the game and CRC is good. Then, using another system function compiled into the ROM, parse the various bytes out into a bunch of settings variables that the game then uses. So, for instance the game will take the above-documented attract sound byte, shift it right by 4 to get the upper nibble, and then store that value in a standard offset into a settings structure. That strucure appears identical per-game. Later, the game will use this parsed settings structure to determine various settings. For example, 4 longs into the settings structure is where the game puts the coin assignment byte. It loads this and compares it against 0x1A at some point to see if the system is in free-play mode.

So, for force settings hacks, you need to track down which bytes mean what in the EEPROM system settings, then find where they're loaded out of the sanitized and checked EEPROM data in order to copy to the parsed setings struct. Then, simply replace the instruction which loads the byte out of the EEPROM with another instruction that just loads the register with a forced value. for example, to set attract-mode sounds forced off in MvsC2, we find out that it loads out of the EEPROM structure with an instruction `0x4063` which effectively means dereference the register pointing at our EEPROM settings, loading the result into R3. So, we can replace it with `0x00e3` which says to move the immediate value `0x00` into register R3. From the above documentation we know that forcing this to a value of `0x10` will force attract sound on all the time, and a value of `0x00` will force attact sound off all the time. Similarly, we can track down the load for offset 3 documented above which is the coin assignments. We replace the instruction `0x4284` (which is a similar indirect load instruction to R0) with `0x1ae0` which loads the immediate value `0x1a` into register R3.

Basically, in so many words: We're locating the common function which parses out the EEPROM structure into the internal structure games use to determine current settings and then forcing various registers to be the exact value we want instead of whatever is in the EEPROM at a given time. It appears that the games all use a common library function to do this, so while the location might be different in different ROMs, the idea is identical and the same patch has worked on all games I've tried it on. I've automated both a free-play patch generator and a no attract sounds patch generator in the repo, found under the `scripts/` directory. With this idea, other common system settings may be forced on with similar skills.

Tracking down how and where the game copies a value is achieved in MAME since it has a working debugger (why does both nulldc and demul not ship with a debugger? How do the emulator authors debug?). Load up a game using debug mode and let it boot all the way to the attract mode. Then, do a search for the serial number found in ofset `0x134` in the ROM header. So, for instance if I wanted to trace MvsC2 I would type the following into the debugger after it booted:

```
find 0xc000000,0x2000000,"BBG0"
```

Then, look at the response. Games tend to copy the two system settings values to a memory location directly preceeding, so you want to look for four copies relatively close to each other in memory. Set a read watchpoint on the exact byte you care about in the first two copies. So, if I was tracking down the free-play parse section in MvsC2, I would write the following. I figured these addresses out by looking at the response to the find command and then looking at the "EEPROM Layout" section above to determine the offset forward or backwards from the 4-byte serial where the byte I care about is.

```
wpset 0xc344d9d,1,r
wpset 0xc344daf,1,r
```

Now, soft-reset the game. You should get a watchpoint hit on both addresses inside the CRC routine (you'll know its this because it does a shit-ton of loops and shifts, and no intersting stores to memory anywhere). Then, the next watchpoint hit will be the function that parses the results. That exact instruction which does the load can be replaced with an alternate instruction which forces the destination register to be the value you previously figured out is what you want (so, for instance `0x1A` for free-play). Use your compiler toolchain that you set up from the above "Compiling Code" section so you can see what the opcode you want is without having to hand-assemble.

Now, you will want to replace the correct bytes in the ROM. For ROM cartridges this is easy since the MMU appears not to be used. Calculate the offset into the main executable by subtracting the load address for the ROM (header offset `0x364`) from the current PC of the instruction you want to replace. This will give you an offset into the main executable. Add the game's load offset to this value (header offset `0x360`) and you should get the raw offset into the ROM. If you did it right, you should find the 2 bytes for the opcode you're replacing at that ROM location. So, for instance in MvsC2 I found the coin mode load instruction at `0xc1F8138`, and subtracted `0xc021000` from it before adding `0x1000` load offset to give me the ROM offset `0x1D8138`. Loading the ROM in a hex editor I observed that the value `0x4284` was indeed at that location and updated it to `0x1AE0` and saved. I tested in demul, nulldc and by net-booting and lo-and-behold, the game was forced to free-play.

For GD-ROM images (and those that were converted to ROMs for net-boot) the above doesn't work due to use of the MMU. So, instead you want to grab about 10 bytes of instructions and search the ROM file for that pattern. You should find only two occurances of that byte string (one in the main executable, one in the test executable). Sometimes, like with MonkeyBall, you will only find one. This is because MonkeyBall does shenanigans with loading the same executable in both test and game mode. Once you've located the offset, make your patch and test!

Auto-hacks for free-play and disabling attract sounds are included in this repo. Adding hacks for other settings is left as an exercise to the reader.

## Making Force Game Settings Hacks

Often you can search the ROM to find the defaults which the game will write if the EEPROM is bad and mess with it there. For instance, in metallic's MvsC2 force character unlock hack, he's found the default game settings and modified the unlock value default so when the game decides to initialize the EEPROM it writes the unlock value (`0x04`) instead of the default (`0x00`). This is why you cannot netboot the force unlock hack directly after a regular copy has been inserted. The game will recognize that its settings are valid and won't rewrite them. If you want to duplicate his success, you can boot a game in an emulator like nulldc or demul and let it write its defaults, then go back and search the value that it's written to find the string in the ROM itself to find where the game stores defaults. Note that often the game has a different default per region (so 8 copies total, since there are 8 slots available for different regions), and stores the defaults in both the main game executable and test mode (for being able to choose factory reset). That means that there might be 16 identical copies of the default game settings. For games that get clever like MonkeyBall, you should expect 8 copies since the test mode and main executable are identical. I haven't found a game like this, but presumably its possible for games to change defaults per-region. So, they might make the game easier in USA region since Americans suck at video games ;P.

If you want to set up different defaults, you can go into the test menu, change your settings, observe the new values and then update your ROM to have those bytes, which will trick the game into initializing with your settings. In this manner you can do things like change the default difficulty, character unlocks and the like. This hasn't been tested on many games, but the theory matches existing force unlock character hacks.
