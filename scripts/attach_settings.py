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
    subparsers = parser.add_subparsers(help='Action to take', dest='action')

    attach_parser = subparsers.add_parser(
        'attach',
        help='Attach a 128-byte EEPRom file to a commercial Naomi ROM.',
        description='Attach a 128-byte EEPRom file to a commercial Naomi ROM.',
    )
    attach_parser.add_argument(
        'rom',
        metavar='ROM',
        type=str,
        help='The Naomi ROM file we should attach the settings to.',
    )
    attach_parser.add_argument(
        'eeprom',
        metavar='EEPROM',
        type=str,
        help='The actual EEPROM settings file we should attach to the ROM.',
    )
    attach_parser.add_argument(
        '--exe',
        metavar='EXE',
        type=str,
        default=os.path.join(root, 'homebrew/settingstrojan/settingstrojan.bin'),
        help='The settings executable that we should attach to the ROM. Defaults to %(default)s.',
    )
    attach_parser.add_argument(
        '--output-file',
        metavar='BIN',
        type=str,
        help='A different file to output to instead of updating the binary specified directly.',
    )
    attach_parser.add_argument(
        '--enable-sentinel',
        action='store_true',
        help='Write a sentinel in main RAM to detect when the same game has had settings changed.',
    )
    attach_parser.add_argument(
        '--enable-debugging',
        action='store_true',
        help='Display debugging information to the screen instead of silently saving settings.',
    )

    extract_parser = subparsers.add_parser(
        'extract',
        help='Extract a 128-byte EEPRom file from a commercial Naomi ROM we have previously attached settings to.',
        description='Extract a 128-byte EEPRom file from a commercial Naomi ROM we have previously attached settings to.',
    )
    extract_parser.add_argument(
        'rom',
        metavar='ROM',
        type=str,
        help='The Naomi ROM file we should extract the settings from.',
    )
    extract_parser.add_argument(
        'eeprom',
        metavar='EEPROM',
        type=str,
        help='The actual EEPROM settings file we should write after extracting from the ROM.',
    )

    info_parser = subparsers.add_parser(
        'info',
        help='Display settings info about a commercial ROM file.',
        description='Display settings info about a commercial ROM file.',
    )
    info_parser.add_argument(
        'rom',
        metavar='ROM',
        type=str,
        help='The Naomi ROM file we should print settings information from.',
    )

    # Grab what we're doing
    args = parser.parse_args()

    if args.action == "attach":
        # Grab the rom, parse it.
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

    elif args.action == "extract":
        # Grab the rom, parse it.
        with open(args.rom, "rb") as fp:
            data = fp.read()

        # Now, search for the settings.
        patcher = NaomiSettingsPatcher(data, b"")
        settings = patcher.get_settings()

        if settings is None:
            print("ROM does not have any settings attached!", file=sys.stderr)
            return 1

        print(f"Wrote settings to {args.eeprom}.")
        with open(args.eeprom, "wb") as fp:
            fp.write(settings)

    elif args.action == "info":
        # Grab the rom, parse it.
        with open(args.rom, "rb") as fp:
            data = fp.read()

        # Now, search for the settings.
        patcher = NaomiSettingsPatcher(data, b"")
        info = patcher.get_info()

        if info is None:
            print("ROM does not have any settings attached!")
        else:
            print(f"ROM has settings attached, with trojan version {info.date.year:04}-{info.date.month:02}-{info.date.day:02}!")
            print(f"Settings change sentinel is {'enabled' if info.enable_sentinel else 'disabled'}.")
            print(f"Debug printing is {'enabled' if info.enable_debugging else 'disabled'}.")
    else:
        print(f"Invalid action {args.action}!", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
