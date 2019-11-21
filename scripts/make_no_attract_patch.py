#! /usr/bin/env python3
import argparse
import os
import os.path
import sys
from typing import List, Optional

from netboot import Binary
from naomi import force_no_attract_sound


def legacy_force_no_attract(data: bytes) -> List[str]:
    # Apply brute-force search for EEPROM parsing routine (doesn't always work).
    newdata = force_no_attract_sound(data)
    return ["# Description: force silent attract mode", *Binary.diff(data, newdata)]


def standard_force_no_attract(data: bytes) -> List[str]:
    def _hex(val: int) -> str:
        out = hex(val)[2:]
        out = out.upper()
        if len(out) == 1:
            out = "0" + out
        return out

    def _output(offset: int, old: Optional[int], new: int) -> str:
        return f"{_hex(offset)}: {_hex(old) if old is not None else '*'} -> {_hex(new)}"

    def _make_differences(region: int) -> List[str]:
        location = 0x1E0 + (0x10 * region)
        values: List[str] = [
            _output(location, None, 0x01),
        ]

        if (data[location + 1] & 0x2) == 0:
            values.append(_output(location + 1, data[location + 1], data[location + 1] | 0x2))
        return values

    # Manually make a differences file since we want to support ROMs that already
    # have enabled EEPROM overrides.
    return [
        "# Description: force silent attract mode",
        f"# File size: {len(data)}",
        *_make_differences(0),
        *_make_differences(1),
        *_make_differences(2),
        *_make_differences(3),
        *_make_differences(4),
    ]


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Utility for creating a forced attract sounds off patch given a Naomi ROM."
    )
    parser.add_argument(
        'bin',
        metavar='BIN',
        type=str,
        help='The binary file we should generate a patch for.',
    )
    parser.add_argument(
        '--patch-file',
        metavar='FILE',
        type=str,
        help='Write patches to a file instead of stdout.',
    )
    parser.add_argument(
        '--mode',
        type=str,
        choices=["legacy", "standard"],
        default="standard",
        help="Mode to use when creating the patch. Defaults to standard mode (header modifications only).",
    )

    # Grab what we're doing
    args = parser.parse_args()

    # Grab the rom, parse it
    with open(args.bin, "rb") as fp:
        data = fp.read()
    if args.mode == "legacy":
        differences = legacy_force_no_attract(data)
    elif args.mode == "standard":
        differences = standard_force_no_attract(data)
    else:
        raise Exception(f"Invalid choice {args.mode}")
    if not args.patch_file:
        for line in differences:
            print(line)
    else:
        with open(args.patch_file, "w") as fp:
            fp.write(os.linesep.join(differences) + os.linesep)

    return 0


if __name__ == "__main__":
    sys.exit(main())
