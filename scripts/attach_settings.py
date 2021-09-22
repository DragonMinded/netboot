#! /usr/bin/env python3
import argparse
import os
import struct
import sys
from typing import Tuple, cast

from naomi import NaomiRom, NaomiRomSection


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


def get_config(data: bytes) -> Tuple[int, int]:
    # Returns a tuple consisting of the original EXE start address and
    # the desired trojan start address.
    for i in range(len(data) - 16):
        if (
            all(x == 0xDD for x in data[i:(i + 4)]) and
            all(x == 0xEE for x in data[(i + 12):(i + 16)])
        ):
            return cast(Tuple[int, int], struct.unpack("<II", data[(i + 4):(i + 12)]))

    raise Exception("Couldn't find config in executable!")


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

    # Now we need to add an EXE init section to the ROM.
    executable = naomi.main_executable
    _, location = get_config(exe)

    for sec in executable.sections:
        if sec.load_address == location:
            # Grab the old entrypoint from the existing modification since the ROM header
            # entrypoint will be the old trojan EXE.
            entrypoint, _ = get_config(data[sec.offset:(sec.offset + sec.length)])
            exe = patch_bytesequence(exe, 0xAA, struct.pack("<I", entrypoint))
            exe = patch_bytesequence(exe, 0xBB, eeprom)

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
