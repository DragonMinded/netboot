#! /usr/bin/env python3
import argparse
import os
import struct
import sys
from typing import Tuple

from naomi import NaomiRom, NaomiRomSection, NaomiEEPRom


# The root of the repo.
root = os.path.realpath(os.path.join(os.path.basename(os.path.realpath(__file__)), ".."))


def change(binfile: bytes, tochange: bytes, loc: int) -> bytes:
    return binfile[:loc] + tochange + binfile[(loc + len(tochange)):]


def patch_bytesequence(data: bytes, sentinel: int, replacement: bytes) -> bytes:
    length = len(replacement)
    for i in range(len(data) - length + 1):
        if all(x == sentinel for x in data[i:(i + length)]):
            return data[:i] + replacement + data[(i + length):]

    raise Exception("Couldn't find spot to patch in data!")


def get_config(data: bytes) -> Tuple[int, int, bool, bool]:
    # Returns a tuple consisting of the original EXE start address and
    # the desired trojan start address, whether sentinel mode is enabled
    # and whether debug printing is enabled.
    for i in range(len(data) - 24):
        if all(x == 0xEE for x in data[i:(i + 4)]) and all(x == 0xEE for x in data[(i + 20):(i + 24)]):
            original_start, trojan_start, sentinel, debug = struct.unpack("<IIII", data[(i + 4):(i + 20)])
            return (
                original_start,
                trojan_start,
                sentinel != 0,
                debug != 0,
            )

    raise Exception("Couldn't find config in executable!")


def get_settings(data: bytes) -> bytes:
    for i in range(len(data) - 128):
        if validate_settings(data[i:(i + 128)]):
            return data[i:(i + 128)]

    raise Exception("Couldn't find settings in executable!")


def validate_settings(data: bytes) -> bool:
    # Returns whether the settings chunk passes CRC.
    sys_section1 = data[2:18]
    sys_section2 = data[20:36]

    game_size1, game_size2 = struct.unpack("<BB", data[38:40])
    if game_size1 != game_size2:
        # These numbers should always match.
        return False

    game_size3, game_size4 = struct.unpack("<BB", data[42:44])
    if game_size3 != game_size4:
        # These numbers should always match.
        return False

    game_section1 = data[44:(44 + game_size1)]
    game_section2 = data[(44 + game_size1):(44 + game_size1 + game_size3)]

    if data[0:2] != NaomiEEPRom.crc(sys_section1):
        # The CRC doesn't match!
        return False
    if data[18:20] != NaomiEEPRom.crc(sys_section2):
        # The CRC doesn't match!
        return False
    if data[36:38] != NaomiEEPRom.crc(game_section1):
        # The CRC doesn't match!
        return False
    if data[40:42] != NaomiEEPRom.crc(game_section2):
        # The CRC doesn't match!
        return False

    # Everything looks good!
    return True


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Utility for attaching pre-selected settings to a commercial Naomi ROM.",
    )
    parser.add_argument(
        'rom',
        metavar='ROM',
        type=str,
        help='The Naomi ROM file we should attach the settings to.',
    )
    parser.add_argument(
        'eeprom',
        metavar='EEPROM',
        type=str,
        help='The actual EEPROM settings files we should attach to the ROM.',
    )
    parser.add_argument(
        '--exe',
        metavar='EXE',
        type=str,
        default=os.path.join(root, 'homebrew/settingstrojan/settingstrojan.bin'),
        help='The settings executable that we should attach to the ROM. Defaults to %(default)s.',
    )
    parser.add_argument(
        '--output-file',
        metavar='BIN',
        type=str,
        help='A different file to output to instead of updating the binary specified directly.',
    )
    parser.add_argument(
        '--enable-sentinel',
        action='store_true',
        help='Write a sentinel in main RAM to detect when the same game has had settings changed.',
    )
    parser.add_argument(
        '--enable-debugging',
        action='store_true',
        help='Display debugging information to the screen instead of silently saving settings.',
    )

    # Grab what we're doing
    args = parser.parse_args()

    # Grab the rom, parse it
    with open(args.rom, "rb") as fp:
        data = fp.read()
    naomi = NaomiRom(data)

    # Grab the attachment. This should be the specific settingstrojan binary blob as compiled
    # out of the homebrew/settingstrojan directory.
    with open(args.exe, "rb") as fp:
        exe = fp.read()

    # First, we need to modiffy the settings trojan with this ROM's load address and
    # the EEPROM we want to add.
    with open(args.eeprom, "rb") as fp:
        eeprom = fp.read()

    if len(eeprom) != 128:
        print("Invalid length of EEPROM file!", file=sys.stderr)
        return 1
    if not validate_settings(eeprom):
        print("EEPROM file is incorrectly formed!", file=sys.stderr)
        return 1
    if naomi.serial != eeprom[3:7] or naomi.serial != eeprom[21:25]:
        print("EEPROM file is not for this game!", file=sys.stderr)
        return 1

    # Now we need to add an EXE init section to the ROM.
    executable = naomi.main_executable
    _, location, _, _ = get_config(exe)

    for sec in executable.sections:
        if sec.load_address == location:
            # Grab the old entrypoint from the existing modification since the ROM header
            # entrypoint will be the old trojan EXE.
            entrypoint, _, _, _ = get_config(data[sec.offset:(sec.offset + sec.length)])
            exe = patch_bytesequence(exe, 0xAA, struct.pack("<I", entrypoint))
            exe = patch_bytesequence(exe, 0xBB, eeprom)
            exe = patch_bytesequence(exe, 0xCF, struct.pack("<I", 1 if args.enable_sentinel else 0))
            exe = patch_bytesequence(exe, 0xDD, struct.pack("<I", 1 if args.enable_debugging else 0))

            # We can reuse this section, but first we need to get rid of the old patch.
            if sec.offset + sec.length == len(data):
                # We can just stick the new file right on top of where the old was.
                print("Overwriting old settings in existing ROM section.")

                # Cut off the old section, add our new section, make sure the length is correct.
                data = data[:sec.offset] + exe
                sec.length = len(exe)
            else:
                # It is somewhere in the middle of an executable, zero it out and
                # then add this section to the end of the ROM.
                print("Zeroing out old settings in existing ROM section and attaching new settings to the end of the file.")

                # Patch the executable with the correct settings and entrypoint.
                data = change(data, b"\0" * sec.length, sec.offset)
                sec.offset = len(data)
                sec.length = len(exe)
                sec.load_address = location
                data += exe
            break
    else:
        if len(executable.sections) >= 8:
            print("ROM already has the maximum number of init sections!", file=sys.stderr)
            return 1

        # Add a new section to the end of the rom for this binary data.
        print("Attaching settings to a new ROM section at the end of the file.")

        executable.sections.append(
            NaomiRomSection(
                offset=len(data),
                load_address=location,
                length=len(exe),
            )
        )

        # Patch the executable with the correct settings and entrypoint.
        exe = patch_bytesequence(exe, 0xAA, struct.pack("<I", executable.entrypoint))
        exe = patch_bytesequence(exe, 0xBB, eeprom)
        exe = patch_bytesequence(exe, 0xCF, struct.pack("<I", 1 if args.enable_sentinel else 0))
        exe = patch_bytesequence(exe, 0xDD, struct.pack("<I", 1 if args.enable_debugging else 0))
        data += exe

    executable.entrypoint = location

    # Generate new header and attach executable to end of data.
    naomi.main_executable = executable

    # Now, just append it to the end of the file
    newdata = naomi.data + data[naomi.HEADER_LENGTH:]

    if args.output_file:
        print(f"Added settings to {args.output_file}.")
        with open(args.output_file, "wb") as fp:
            fp.write(newdata)
    else:
        print(f"Added settings to {args.rom}.")
        with open(args.rom, "wb") as fp:
            fp.write(newdata)

    return 0


if __name__ == "__main__":
    sys.exit(main())
