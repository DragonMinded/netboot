#! /usr/bin/env python3
import argparse
import os
import sys

from naomi import NaomiSettingsPatcher


# The root of the repo.
root = os.path.realpath(os.path.join(os.path.basename(os.path.realpath(__file__)), ".."))


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

    # Grab the attachment. This should be the specific settingstrojan binary blob as compiled
    # out of the homebrew/settingstrojan directory.
    with open(args.exe, "rb") as fp:
        exe = fp.read()

    # First, we need to modiffy the settings trojan with this ROM's load address and
    # the EEPROM we want to add.
    with open(args.eeprom, "rb") as fp:
        eeprom = fp.read()

    # Now, patch it onto the data.
    patcher = NaomiSettingsPatcher(data, exe)
    patcher.put_settings(eeprom, enable_sentinel=args.enable_sentinel, enable_debugging=args.enable_debugging, verbose=True)

    if args.output_file:
        print(f"Added settings to {args.output_file}.")
        with open(args.output_file, "wb") as fp:
            fp.write(patcher.data)
    else:
        print(f"Added settings to {args.rom}.")
        with open(args.rom, "wb") as fp:
            fp.write(patcher.data)

    return 0


if __name__ == "__main__":
    sys.exit(main())
