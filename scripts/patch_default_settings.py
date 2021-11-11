#! /usr/bin/env python3
import argparse
import os
import sys

from arcadeutils import FileBytes, BinaryDiff
from naomi import NaomiEEPRom, NaomiRom
from naomi.settings import SettingsManager


# The root of the repo.
root = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), ".."))


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Command-Line Utility for patching different game defaults into a Naomi ROM.",
    )
    parser.add_argument(
        'rom',
        metavar='ROM',
        type=str,
        help='The ROM we should generate a patch for.',
    )
    parser.add_argument(
        'eeprom',
        metavar='EEPROM',
        type=str,
        help='The EEPROM settings file we should use to generate the patch.',
    )
    parser.add_argument(
        '--output-file',
        metavar='BIN',
        type=str,
        default=None,
        help='A different file to output to instead of updating the ROM specified directly.',
    )
    parser.add_argument(
        '--patch-file',
        metavar='PATCH',
        type=str,
        default=None,
        help='Write changed bytes to a patch instead of generating a new ROM.',
    )
    parser.add_argument(
        '--settings-directory',
        metavar='DIR',
        type=str,
        default=os.path.join(root, 'naomi', 'settings', 'definitions'),
        help='The directory containing settings definition files. Defaults to %(default)s.',
    )

    # Grab what we're doing
    args = parser.parse_args()

    if args.output_file and args.patch_file:
        raise Exception("Cannot write both a patch and a new ROM!")

    # First, try to open the EEPRom file.
    with open(args.eeprom, "rb") as fp:
        eeprom = NaomiEEPRom(fp.read())
        manager = SettingsManager(args.settings_directory)
        defaults = manager.from_serial(eeprom.serial)
        defaulteeprom = NaomiEEPRom(manager.to_eeprom(defaults))

    with open(args.rom, "rb" if args.output_file else "rb+") as fp:  # type: ignore
        data = FileBytes(fp)
        original = data.clone()
        rom = NaomiRom(data)

        defaultbytes = defaulteeprom.game.data
        updatedbytes = eeprom.game.data

        if len(defaultbytes) != len(updatedbytes):
            raise Exception("EEPROM sections aren't the same length!")

        for exe in [rom.main_executable, rom.test_executable]:
            for section in exe.sections:
                start = section.offset
                end = section.offset + section.length
                print(f"Searching {start} to {end}...")

                while True:
                    found = data.search(defaultbytes, start=start, end=end)

                    if found is not None:
                        print(f"Patching offset {found}!")
                        data[found:(found + len(updatedbytes))] = updatedbytes
                        start = found + 1
                    else:
                        # Done!
                        break

        if args.patch_file:
            print(f"Generating EEPROM settings patch and writing to {args.patch_file}.")
            changes = ["# Description: patch default game settings", *BinaryDiff.diff(original, data)]
            with open(args.patch_file, "w") as fps:
                fps.write(os.linesep.join(changes) + os.linesep)
        else:
            if args.output_file:
                print(f"Patched default game EEPROM settings to {args.output_file}.")
                with open(args.output_file, "wb") as fp:
                    data.write_changes(fp)
            else:
                print(f"Patched default game EEPROM settings to {args.rom}.")
                data.write_changes()

    return 0


if __name__ == "__main__":
    sys.exit(main())
