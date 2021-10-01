#! /usr/bin/env python3
import argparse
import os
import sys

from settings import SettingsEditor, SettingsManager, SettingsParseException, SettingsSaveException


# The root of the repo.
root = os.path.realpath(os.path.join(os.path.basename(os.path.realpath(__file__)), ".."))


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Command-Line Utility for editing a Naomi settings file.",
    )
    parser.add_argument(
        'eeprom',
        metavar='EEPROM',
        type=str,
        help='The EEPROM settings file we should edit.',
    )
    parser.add_argument(
        '--settings-directory',
        metavar='DIR',
        type=str,
        default=os.path.join(root, 'settings/definitions'),
        help='The directory containing settings definition files.',
    )
    parser.add_argument(
        '--serial',
        metavar='SERIAL',
        type=str,
        help='The serial number of the game, if the EEPROM file does not exist.',
    )

    # Grab what we're doing
    args = parser.parse_args()

    # First, try to open the EEPRom file. If it does not exist, then we need to create a default.
    manager = SettingsManager(args.settings_directory)
    try:
        try:
            with open(args.eeprom, "rb") as fp:
                data = fp.read()
            settings = manager.from_eeprom(data)
        except OSError:
            settings = manager.from_serial(args.serial.encode('ascii'))
    except (SettingsParseException, SettingsSaveException) as e:
        print(f"Error in \"{e.filename}\":", str(e), file=sys.stderr)
        return 1

    # Now, invoke the CLI editor so settings can be edited.
    editor = SettingsEditor(settings)
    editor.run()

    # Now, write out the EEPROM.
    eeprom = manager.to_eeprom(settings)
    with open(args.eeprom, "wb") as fp:
        fp.write(eeprom)

    return 0


if __name__ == "__main__":
    sys.exit(main())
