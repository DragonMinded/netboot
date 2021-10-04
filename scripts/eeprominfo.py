#! /usr/bin/env python3
import argparse
import os
import sys

from naomi import NaomiEEPRom
from naomi.settings import SettingsManager, ReadOnlyCondition, SettingsParseException, SettingsSaveException


# The root of the repo.
root = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), ".."))


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Utility for printing EEPROM hex digits for a game's settings section in an EEPRom file.",
    )
    parser.add_argument(
        'eeprom',
        metavar='EEPROM',
        type=str,
        help='The actual EEPROM settings file we should attach to the ROM.',
    )
    parser.add_argument(
        '--display-parsed-settings',
        action="store_true",
        help="Attempt to parse EEPROM and display settings as well.",
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

    with open(args.eeprom, "rb") as fp:
        data = fp.read()

    eeprom = NaomiEEPRom(data)
    if eeprom.game.valid:
        gamehexstr = eeprom.game.data.hex()
        gamechunks = [gamehexstr[i:(i + 2)] for i in range(0, len(gamehexstr), 2)]
        print(f"Serial: {eeprom.serial.decode('ascii')}")
        print(f"Game Settings Hex: {' '.join(gamechunks)}")

        if args.display_parsed_settings:
            # Grab the actual EEPRom so we can print the settings within.
            manager = SettingsManager(args.settings_directory)

            try:
                config = manager.from_eeprom(data)

                print("Parsed Game Settings:")

                if config.game.settings:
                    for setting in config.game.settings:
                        # Don't show read-only settints.
                        if setting.read_only is True:
                            continue
                        if isinstance(setting.read_only, ReadOnlyCondition):
                            if setting.read_only.evaluate(config.game.settings):
                                continue

                        # This shouldn't happen, but make mypy happy.
                        if setting.current is None:
                            continue

                        print(f"  {setting.name}: {setting.values[setting.current]}")
                else:
                    print("  No game settings, game will use its own defaults.")
            except (SettingsParseException, SettingsSaveException) as e:
                print(f"Error in \"{e.filename}\":", str(e), file=sys.stderr)
                return 1

    else:
        print(f"Serial: {eeprom.serial.decode('ascii')}, Game Settings: INVALID")

    return 0


if __name__ == "__main__":
    sys.exit(main())
