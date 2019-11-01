#! /usr/bin/env python3
import argparse
import os
import os.path
import struct
import sys

from netboot import Binary
from naomi import NaomiRom, NaomiEEPRom


def _patch_bytesequence(data: bytes, sentinel: int, replacement: bytes) -> bytes:
    length = len(replacement)
    for i in range(len(data) - length + 1):
        if all(x == sentinel for x in data[i:(i + length)]):
            return data[:i] + replacement + data[(i + length):]

    raise Exception("Couldn't find spot to patch in data!")


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Utility for creating a free-play patch given a Naomi ROM."
    )
    parser.add_argument(
        'bin',
        metavar='BIN',
        type=str,
        help='The binary file we should generate a patch for.',
    )
    parser.add_argument(
        '--patch-file',
        metavar='FILE',
        type=str,
        help='Write patches to a file instead of stdout.',
    )

    # Grab what we're doing
    args = parser.parse_args()
    current_directory = os.path.abspath(os.path.dirname(__file__))
    assembly_directory = os.path.join(current_directory, '../assembly')
    springboard_file = os.path.join(assembly_directory, 'springboard.bin')
    write_eeprom_file = os.path.join(assembly_directory, 'write_eeprom.bin')

    # Grab the rom, parse it
    with open(args.bin, "rb") as fp:
        data = fp.read()
    rom = NaomiRom(data)
    if not rom.valid:
        print("ROM file does not appear to be a Naomi netboot ROM!", file=sys.stderr)
        return 1

    # Grab the patches we are going to apply
    with open(springboard_file, "rb") as fp:
        springboard = fp.read()
    with open(write_eeprom_file, "rb") as fp:
        write_eeprom = fp.read()

    # First, grab the EEProm data we need to write
    eeprom = NaomiEEPRom.default(rom.serial)
    eeprom[6] = b"\x1A"
    chunk = eeprom.data + eeprom.data + bytes([0xFF] * 12)

    # Now, find an acceptible spot to patch this in the rom
    exec_data = rom.main_executable
    patch_location = None
    for rloc in range(exec_data.length):
        # Reverse the search so we go end to front
        loc = exec_data.length - (rloc + 1)

        # First, make sure the in-memory location will be aligned to 32 bytes.
        # This is because maple bus commands need that, and we've been lazy and
        # aligned the maple bus buffer in the patch at a 32-byte alignment.
        if (loc + exec_data.load_address) % 32 != 0:
            continue

        # Second, see if the section is long enough for our patch data
        spot = data[(loc + exec_data.offset):(loc + exec_data.offset + len(write_eeprom))]
        if len(spot) != len(write_eeprom):
            continue

        # Finally, make sure its empty
        if all(x == 0xFF for x in spot) or all(x == 0x00 for x in spot):
            # Its a match!
            patch_location = loc
            break

    if patch_location is None:
        print("ROM file does not have a suitable spot for the patch!", file=sys.stderr)
        return 1

    # Calculate physical patch offset as well as jump point
    patch_offset = patch_location + exec_data.offset
    patch_jumppoint = patch_location + exec_data.load_address

    # I'm not sure if this is a safe spot in RAM, but choosing it for now anyway.
    buffer_location = (exec_data.load_address - (3 * 1024)) & 0xFFFFFFE0

    # Calculate where the entrypoint is inside the executable
    springboard_offset = exec_data.entrypoint - exec_data.load_address
    if springboard_offset < 0 or springboard_offset >= (exec_data.length - 4):
        raise Exception("Entrypoint is somehow outside of main executable?")
    springboard_offset += exec_data.offset

    # Patch the write_eeprom data to include the personalized eeprom chunk we generated before
    write_eeprom = _patch_bytesequence(write_eeprom, 0xAA, chunk)

    # Patch the entrypoint where we will put the springboard so we can jump back when we're done
    write_eeprom = _patch_bytesequence(write_eeprom, 0xBB, struct.pack("<I", exec_data.entrypoint))

    # Patch in the bytes in the entrypoint that the springboard will clobber
    write_eeprom = _patch_bytesequence(write_eeprom, 0xCC, data[springboard_offset:(springboard_offset + 12)])

    # Now, create a final springboard that jumps to the patch
    springboard = _patch_bytesequence(springboard, 0xDD, struct.pack("<I", patch_jumppoint))

    # Patch a safe memory location to use as a maple command/response buffer
    write_eeprom = _patch_bytesequence(write_eeprom, 0xEE, struct.pack("<I", buffer_location))

    # Now, generate a patch with this updated data overlaid on the original rom
    newdata = (
        data[:springboard_offset] +
        springboard +
        data[(springboard_offset + len(springboard)):patch_offset] +
        write_eeprom +
        data[(patch_offset + len(write_eeprom)):]
    )

    # TEMP: Create a personalized write patch for trying in MAME
    with open(write_eeprom_file + ".patched", "wb") as fp:
        fp.write(write_eeprom)

    # TEMP: Create a personalized springboard for trying in MAME
    with open(springboard_file + ".patched", "wb") as fp:
        fp.write(springboard)

    # TEMP: Outout the locations into main memory where they should be loaded in MAME
    print("springboard:", hex(exec_data.entrypoint))
    print("main patch:", hex(patch_jumppoint))

    differences = Binary.diff(data, newdata)
    if not args.patch_file:
        for line in differences:
            print(line)
    else:
        with open(args.patch_file, "w") as fp:
            fp.write(os.linesep.join(differences))

    return 0


if __name__ == "__main__":
    sys.exit(main())
