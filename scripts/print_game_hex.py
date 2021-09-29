#! /usr/bin/env python3
import argparse
import sys

from naomi import NaomiEEPRom


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Utility for printing hex digits for a game's settings section in an EEPRom file.",
    )
    parser.add_argument(
        'eeprom',
        metavar='EEPROM',
        type=str,
        help='The actual EEPROM settings file we should attach to the ROM.',
    )

    # Grab what we're doing
    args = parser.parse_args()

    with open(args.eeprom, "rb") as fp:
        data = fp.read()

    eeprom = NaomiEEPRom(data)
    hexstr = eeprom.game.data.hex()
    chunks = [hexstr[i:(i + 2)] for i in range(0, len(hexstr), 2)]
    print(" ".join(chunks))

    return 0


if __name__ == "__main__":
    sys.exit(main())
