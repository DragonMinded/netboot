#! /usr/bin/env python3
import argparse
import sys

from naomi import NaomiRom, NaomiRomSection


SRAM_LOCATION = 0x200000
SRAM_SIZE = 32768


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description=(
            "Utility for attaching an SRAM dump to an Atomiswave conversion Naomi ROM. "
            "Use this to set up preferred settings in an emulator, and then send those "
            "settings to your Naomi when you netboot."
        ),
    )
    parser.add_argument(
        'bin',
        metavar='BIN',
        type=str,
        help='The binary file we should attach the SRAM to.',
    )
    parser.add_argument(
        'sram',
        metavar='SRAM',
        type=str,
        help='The SRAM file we should attach to the binary.',
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
    with open(args.bin, "rb") as fp:
        data = fp.read()
    naomi = NaomiRom(data)

    # Grab the SRAM
    with open(args.sram, "rb") as fp:
        sram = fp.read()
    if len(sram) != SRAM_SIZE:
        print(f"SRAM file is not the right size, should be {SRAM_SIZE} bytes!", file=sys.stderr)
        return 1

    # First, find out if there's already an SRAM portion to the file
    executable = naomi.main_executable
    for section in executable.sections:
        if section.load_address == SRAM_LOCATION:
            # This is a SRAM load chunk
            if section.length != SRAM_SIZE:
                print("Found SRAM init section, but it is the wrong size!", file=sys.stderr)
                return 1

            # We can just update the data to overwrite this section
            newdata = data[:section.offset] + sram + data[(section.offset + section.length):]
            break
    else:
        # We need to add a SRAM init section to the ROM
        if len(executable.sections) >= 8:
            print("ROM already has the maximum number of init sections!", file=sys.stderr)
            return 1

        # Add a new section to the end of the rom for this SRAM section
        executable.sections.append(
            NaomiRomSection(
                offset=len(data),
                load_address=SRAM_LOCATION,
                length=SRAM_SIZE,
            )
        )
        naomi.main_executable = executable

        # Now, just append it to the end of the file
        newdata = naomi.data + data[naomi.HEADER_LENGTH:] + sram

    if args.output_file:
        print(f"Added SRAM init to the end of {args.output_file}.", file=sys.stderr)
        with open(args.output_file, "wb") as fp:
            fp.write(newdata)
    else:
        print(f"Added SRAM init to the end of {args.bin}.", file=sys.stderr)
        with open(args.bin, "wb") as fp:
            fp.write(newdata)

    return 0


if __name__ == "__main__":
    sys.exit(main())
