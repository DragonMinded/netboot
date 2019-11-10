#! /usr/bin/env python3
import argparse
import os
import os.path
import sys

from netboot import Binary
from naomi import NaomiRom


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

    # Grab the rom, parse it
    with open(args.bin, "rb") as fp:
        data = fp.read()
    rom = NaomiRom(data)
    if not rom.valid:
        print("ROM file does not appear to be a Naomi netboot ROM!", file=sys.stderr)
        return 1

    # Now, find the location of our magic string in the main executable.
    exec_data = rom.main_executable
    patch_location = None
    for rloc in range(exec_data.length):
        loc = rloc + exec_data.offset
        if data[loc:(loc + 10)] == bytes([0x40, 0x63, 0x12, 0xe2, 0xec, 0x32, 0x3c, 0x63, 0x09, 0x43]):
            patch_location = loc
            break

    if patch_location is None:
        print("ROM file does not have a suitable spot for the patch!", file=sys.stderr)
        return 1

    # Now, generate a patch with this updated data overlaid on the original rom
    newdata = (
        data[:patch_location] +
        bytes([0x00, 0xe3]) +
        data[(patch_location + 2):]
    )

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
