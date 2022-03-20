#! /usr/bin/env python3
import argparse
import os
import sys

from arcadeutils import FileBytes
from naomi import NaomiRomRegionEnum, NaomiSettingsPatcher
from naomi.settings import NaomiSettingsEditor, NaomiSettingsManager, ReadOnlyCondition, SettingsParseException, SettingsSaveException


# The root of the repo.
root = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), ".."))


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Utility for attaching, extracting and editing pre-selected EEPROM settings to a commercial Naomi ROM.",
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
        help='The Naomi ROM file we should attach the EEPROM settings to.',
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
        default=os.path.join(root, 'homebrew', 'settingstrojan', 'settingstrojan.bin'),
        help='The settings executable that we should attach to the ROM. Defaults to %(default)s.',
    )
    attach_parser.add_argument(
        '--output-file',
        metavar='BIN',
        type=str,
        help='A different file to output to instead of updating the binary specified directly.',
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
        help='The Naomi ROM file we should extract the EEPROM settings from.',
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
        help='The Naomi ROM file we should print EEPROM settings information from.',
    )
    info_parser.add_argument(
        '--settings-directory',
        metavar='DIR',
        type=str,
        default=os.path.join(root, 'naomi', 'settings', 'definitions'),
        help='The directory containing settings definition files. Defaults to %(default)s.',
    )

    edit_parser = subparsers.add_parser(
        'edit',
        help='Created or edit a 128-byte EEPRom settings file and attach it to a commercial Naomi ROM.',
        description='Created or edit a 128-byte EEPRom settings file and attach it to a commercial Naomi ROM.',
    )
    edit_parser.add_argument(
        'rom',
        metavar='ROM',
        type=str,
        help='The Naomi ROM file we should edit the EEPROM settings for.',
    )
    edit_parser.add_argument(
        '--exe',
        metavar='EXE',
        type=str,
        default=os.path.join(root, 'homebrew', 'settingstrojan', 'settingstrojan.bin'),
        help='The settings executable that we should attach to the ROM. Defaults to %(default)s.',
    )
    edit_parser.add_argument(
        '--settings-directory',
        metavar='DIR',
        type=str,
        default=os.path.join(root, 'naomi', 'settings', 'definitions'),
        help='The directory containing settings definition files. Defaults to %(default)s.',
    )
    edit_parser.add_argument(
        '--output-file',
        metavar='BIN',
        type=str,
        help='A different file to output to instead of updating the binary specified directly.',
    )
    edit_parser.add_argument(
        '--region',
        metavar="REGION",
        type=str,
        help='The region the Naomi which will boot this ROM is set to. Defaults to "japan".',
    )
    edit_parser.add_argument(
        '--enable-debugging',
        action='store_true',
        help='Display debugging information to the screen instead of silently saving settings.',
    )

    # Grab what we're doing
    args = parser.parse_args()

    if args.action == "attach":
        # Grab the rom, parse it.
        with open(args.rom, "rb" if args.output_file else "rb+") as fp:
            data = FileBytes(fp)  # type: ignore

            # Grab the attachment. This should be the specific settingstrojan binary blob as compiled
            # out of the homebrew/settingstrojan directory.
            with open(args.exe, "rb") as fp:
                exe = fp.read()

            # First, we need to modiffy the settings trojan with this ROM's load address and
            # the EEPROM we want to add.
            with open(args.eeprom, "rb") as fp:
                eeprom = fp.read()

            # Check some bounds.
            if len(eeprom) != NaomiSettingsPatcher.EEPROM_SIZE:
                print("EEPROM is the wrong size! Perhaps you meant to use \"attach_sram\"?", file=sys.stderr)
                return 1

            # Now, patch it onto the data.
            patcher = NaomiSettingsPatcher(data, exe)
            patcher.put_eeprom(eeprom, enable_debugging=args.enable_debugging, verbose=True)

            if args.output_file:
                print(f"Added EEPROM settings to {args.output_file}.")
                with open(args.output_file, "wb") as fp:
                    patcher.data.write_changes(fp)
            else:
                print(f"Added EEPROM settings to {args.rom}.")
                patcher.data.write_changes()

    elif args.action == "extract":
        # Grab the rom, parse it.
        with open(args.rom, "rb") as rfp:
            data = FileBytes(rfp)

            # Now, search for the settings.
            patcher = NaomiSettingsPatcher(data, None)
            settings = patcher.get_eeprom()

            if settings is None:
                print("ROM does not have any EEPROM settings attached!", file=sys.stderr)
                return 1

            if len(settings) != NaomiSettingsPatcher.EEPROM_SIZE:
                print("EEPROM is the wrong size! Perhaps you meant to use \"attach_sram\"?", file=sys.stderr)
                return 1

            print(f"Wrote EEPROM settings to {args.eeprom}.")
            with open(args.eeprom, "wb") as wfp:
                wfp.write(settings)

    elif args.action == "info":
        # Grab the rom, parse it.
        with open(args.rom, "rb") as fp:
            data = FileBytes(fp)

            # Now, search for the settings.
            patcher = NaomiSettingsPatcher(data, None)
            info = patcher.eeprom_info

            if info is None:
                print("ROM does not have any EEPROM settings attached!")
            else:
                print(f"ROM has EEPROM settings attached, with trojan version {info.date.year:04}-{info.date.month:02}-{info.date.day:02}!")
                print(f"Debug printing is {'enabled' if info.enable_debugging else 'disabled'}.")

                # Grab the actual EEPRom so we can print the settings within.
                manager = NaomiSettingsManager(args.settings_directory)
                eepromdata = patcher.get_eeprom()
                config = None

                try:
                    if eepromdata is not None:
                        try:
                            config = manager.from_eeprom(eepromdata)
                        except FileNotFoundError:
                            # We don't have the directory configured, so skip this.
                            pass

                    if config is not None:
                        print("System Settings:")

                        for setting in config.system.settings:
                            # Don't show read-only settints.
                            if setting.read_only is True:
                                continue
                            if isinstance(setting.read_only, ReadOnlyCondition):
                                if setting.read_only.evaluate(config.system.settings):
                                    continue

                            # This shouldn't happen, but make mypy happy.
                            if setting.current is None:
                                continue

                            print(f"  {setting.name}: {setting.values[setting.current]}")

                        print("Game Settings:")

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

    elif args.action == "edit":
        # Grab the rom, parse it.
        with open(args.rom, "rb" if args.output_file else "rb+") as fp:
            data = FileBytes(fp)  # type: ignore

            # Grab the attachment. This should be the specific settingstrojan binary blob as compiled
            # out of the homebrew/settingstrojan directory.
            with open(args.exe, "rb") as fp:
                exe = fp.read()

            # First, try to extract existing eeprom for editing.
            patcher = NaomiSettingsPatcher(data, exe)
            eepromdata = patcher.get_eeprom()

            manager = NaomiSettingsManager(args.settings_directory)
            if eepromdata is None:
                # We need to make them up from scratch.
                region = {
                    "japan": NaomiRomRegionEnum.REGION_JAPAN,
                    "usa": NaomiRomRegionEnum.REGION_USA,
                    "export": NaomiRomRegionEnum.REGION_EXPORT,
                    "korea": NaomiRomRegionEnum.REGION_KOREA,
                    "australia": NaomiRomRegionEnum.REGION_AUSTRALIA,
                }.get(args.region, NaomiRomRegionEnum.REGION_JAPAN)
                parsedsettings = manager.from_rom(patcher.rom, region=region)
            else:
                # We have an eeprom to edit.
                parsedsettings = manager.from_eeprom(eepromdata)

            # Now, edit those created or extracted settings.
            editor = NaomiSettingsEditor(parsedsettings)
            if editor.run():
                # If the editor signals to us that the user wanted to save the settings
                # then we should patch them into the binary.
                eepromdata = manager.to_eeprom(parsedsettings)
                patcher.put_eeprom(
                    eepromdata,
                    enable_debugging=args.enable_debugging,
                    verbose=True,
                )

                if args.output_file:
                    print(f"Added EEPROM settings to {args.output_file}.")
                    with open(args.output_file, "wb") as fp:
                        patcher.data.write_changes(fp)
                else:
                    print(f"Added EEPROM settings to {args.rom}.")
                    patcher.data.write_changes()

    else:
        print(f"Invalid action {args.action}!", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
