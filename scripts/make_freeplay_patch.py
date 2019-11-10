#! /usr/bin/env python3
import argparse
import os
import os.path
import sys

from netboot import Binary
from naomi import force_freeplay


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Utility for creating a forced free-play on patch given a Naomi ROM."
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
    newdata = force_freeplay(data)
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
