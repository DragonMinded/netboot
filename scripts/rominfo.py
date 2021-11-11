#! /usr/bin/env python3
import argparse
import sys
from typing import Dict

from arcadeutils import FileBytes
from naomi import NaomiRom, NaomiRomRegionEnum


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Utility for printing information about a ROM file.",
    )
    parser.add_argument(
        'bin',
        metavar='BIN',
        type=str,
        help='The binary file we should generate info for.',
    )

    # Grab what we're doing
    args = parser.parse_args()

    # Grab the rom, parse it
    with open(args.bin, "rb") as fp:
        data = FileBytes(fp)

        # Create a text LUT
        region_lut: Dict[NaomiRomRegionEnum, str] = {
            NaomiRomRegionEnum.REGION_JAPAN: "Japan",
            NaomiRomRegionEnum.REGION_USA: "USA",
            NaomiRomRegionEnum.REGION_EXPORT: "Export",
            NaomiRomRegionEnum.REGION_KOREA: "Korea",
            NaomiRomRegionEnum.REGION_AUSTRALIA: "Australia",
        }

        # First, assume its a Naomi ROM
        naomi = NaomiRom(data)
        if naomi.valid:
            print("NAOMI ROM")
            print("=========")
            print(f"Publisher:       {naomi.publisher}")
            print(f"Japan Title:     {naomi.names[NaomiRomRegionEnum.REGION_JAPAN]}")
            print(f"USA Title:       {naomi.names[NaomiRomRegionEnum.REGION_USA]}")
            print(f"Export Title:    {naomi.names[NaomiRomRegionEnum.REGION_EXPORT]}")
            print(f"Korea Title:     {naomi.names[NaomiRomRegionEnum.REGION_KOREA]}")
            print(f"Australia Title: {naomi.names[NaomiRomRegionEnum.REGION_AUSTRALIA]}")
            print(f"Publish Date:    {naomi.date}")
            print(f"Serial Number:   {naomi.serial.decode('ascii')}")
            print(f"ROM Size:        {len(data)} bytes")
            print("")

            print("Supported Configurations")
            print("------------------------")
            print(f"Regions:         {', '.join(region_lut[r] for r in naomi.regions)}")
            print(f"Players:         {', '.join(str(p) for p in naomi.players)}")
            print(f"Monitor:         {', '.join(str(f) + 'khz' for f in naomi.frequencies)}")
            print(f"Orientation:     {', '.join(o for o in naomi.orientations)}")
            print(f"Service Type:    {naomi.servicetype}")
            print("")

            print("Main Executable Sections")
            print("------------------------")
            for section in naomi.main_executable.sections:
                print(f"ROM Offset:      {hex(section.offset)}")
                print(f"Memory Offset:   {hex(section.load_address)}")
                print(f"Section Length:  {section.length} bytes")
                print("")
            print(f"Entrypoint:      {hex(naomi.main_executable.entrypoint)}")
            print("")

            print("Test Executable Sections")
            print("------------------------")
            for section in naomi.test_executable.sections:
                print(f"ROM Offset:      {hex(section.offset)}")
                print(f"Memory Offset:   {hex(section.load_address)}")
                print(f"Section Length:  {section.length} bytes")
                print("")
            print(f"Entrypoint:      {hex(naomi.test_executable.entrypoint)}")
            print("")

            print("Per-Region EEPROM Defaults")
            print("--------------------------")
            for region, default in naomi.defaults.items():
                print(f"{region_lut[region]}")
                if not default.apply_settings:
                    print("Override:        disabled")
                else:
                    print("Override:        enabled")
                    print(f"Force vertical:  {'yes' if default.force_vertical else 'no'}")
                    print(f"Force silent:    {'yes' if default.force_silent else 'no'}")
                    print(f"Chute type:      {default.chute}")
                    if default.coin_setting < 27:
                        setting = f"#{default.coin_setting}"
                    elif default.coin_setting == 27:
                        setting = "free play"
                    elif default.coin_setting == 28:
                        setting = "manual assignment"
                    print(f"Coin setting:    {setting}")
                    if default.coin_setting == 28:
                        print(f"Coin 1 rate:     {default.coin_1_rate}")
                        print(f"Coin 2 rate:     {default.coin_2_rate}")
                        print(f"Credit rate:     {default.credit_rate}")
                        print(f"Bonus:           {default.bonus}")
                    for i, text in enumerate(default.sequences):
                        print(f"Sequence {i + 1}:      {text}")
                print("")

            return 0

    # Couldn't figure out ROM type
    print("Couldn't determine ROM type!", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
