#! /usr/bin/env python3
import argparse
import sys

from arcadeutils import FileBytes
from naomi import NaomiSettingsPatcher


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description=(
            "Utility for attaching an SRAM dump to an Atomiswave conversion Naomi ROM. "
            "Use this to set up preferred settings in an emulator, and then send those "
            "settings to your Naomi when you netboot."
        ),
    )
    subparsers = parser.add_subparsers(help='Action to take', dest='action')

    attach_parser = subparsers.add_parser(
        'attach',
        help='Attach a 32K SRAM file to a commercial Naomi ROM.',
        description='Attach a 32K SRAM file to a commercial Naomi ROM.',
    )
    attach_parser.add_argument(
        'bin',
        metavar='BIN',
        type=str,
        help='The binary file we should attach the SRAM settings to.',
    )
    attach_parser.add_argument(
        'sram',
        metavar='SRAM',
        type=str,
        help='The SRAM settings file we should attach to the binary.',
    )
    attach_parser.add_argument(
        '--output-file',
        metavar='BIN',
        type=str,
        help='A different file to output to instead of updating the binary specified directly.',
    )

    extract_parser = subparsers.add_parser(
        'extract',
        help='Extract a 32K SRAM file from a commercial Naomi ROM.',
        description='Extract a 32K SRAM file from a commercial Naomi ROM.',
    )
    extract_parser.add_argument(
        'bin',
        metavar='BIN',
        type=str,
        help='The binary file we should extract the SRAM settings from.',
    )
    extract_parser.add_argument(
        'sram',
        metavar='SRAM',
        type=str,
        help='The SRAM settings file we should extract from the binary.',
    )

    # Grab what we're doing
    args = parser.parse_args()

    if args.action == "attach":
        # Grab the rom, parse it
        with open(args.bin, "rb" if args.output_file else "rb+") as fp:
            data = FileBytes(fp)

            with open(args.sram, "rb") as fp:
                sram = fp.read()

            if len(sram) != NaomiSettingsPatcher.SRAM_SIZE:
                print(f"SRAM file is not the right size, should be {NaomiSettingsPatcher.SRAM_SIZE} bytes!", file=sys.stderr)
                return 1

            patcher = NaomiSettingsPatcher(data, None)
            patcher.put_sram(sram, verbose=True)

            if args.output_file:
                print(f"Added SRAM init to the end of {args.output_file}.", file=sys.stderr)
                with open(args.output_file, "wb") as fp:
                    patcher.data.write_changes(fp)
            else:
                print(f"Added SRAM init to the end of {args.bin}.", file=sys.stderr)
                patcher.data.write_changes()

    elif args.action == "extract":
        # Grab the rom, parse it.
        with open(args.bin, "rb") as rfp:
            data = FileBytes(rfp)

            # Now, search for the settings.
            patcher = NaomiSettingsPatcher(data, None)
            settings = patcher.get_sram()

            if settings is None:
                print("ROM does not have any SRAM settings attached!", file=sys.stderr)
                return 1

            if len(settings) != NaomiSettingsPatcher.SRAM_SIZE:
                print("SRAM is the wrong size! Perhaps you meant to use \"attach_settings\"?", file=sys.stderr)
                return 1

            print(f"Wrote SRAM settings to {args.sram}.")
            with open(args.sram, "wb") as wfp:
                wfp.write(settings)

    return 0


if __name__ == "__main__":
    sys.exit(main())
