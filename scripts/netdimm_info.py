#!/usr/bin/env python3
# Triforce Netfirm Toolbox, put into the public domain.
# Please attribute properly, but only if you want.
import argparse
import enum
import sys
from netboot import NetDimm, TargetEnum
from typing import Any


class EnumAction(argparse.Action):
    """
    Argparse action for handling Enums
    """
    def __init__(self, **kwargs: Any):
        # Pop off the type value
        enum_type = kwargs.pop("type", None)

        # Ensure an Enum subclass is provided
        if enum_type is None:
            raise ValueError("type must be assigned an Enum when using EnumAction")
        if not issubclass(enum_type, enum.Enum):
            raise TypeError("type must be an Enum when using EnumAction")

        # Generate choices from the Enum
        kwargs.setdefault("choices", tuple(e.value for e in enum_type))

        super(EnumAction, self).__init__(**kwargs)

        self._enum = enum_type

    def __call__(self, parser: Any, namespace: Any, values: Any, option_string: Any = None) -> None:
        # Convert value back into an Enum
        value = self._enum(values)
        setattr(namespace, self.dest, value)


def main() -> int:
    parser = argparse.ArgumentParser(description="Tools for requesting info from a NetDimm for Naomi/Chihiro/Triforce.")
    parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm is configured on.",
    )
    parser.add_argument(
        "--target",
        metavar="TARGET",
        type=TargetEnum,
        action=EnumAction,
        default=TargetEnum.TARGET_NAOMI,
        help="Target platform this image is going to. Defaults to 'naomi'. Choose from 'naomi', 'chihiro' or 'triforce'.",
    )

    args = parser.parse_args()

    print("Requesting...", file=sys.stderr)
    netdimm = NetDimm(args.ip, target=args.target)
    info = netdimm.info()
    print(f"DIMM Firmware Version: {info.firmware_version.value}")
    print(f"DIMM Memory Size: {info.memory_size} MB")
    print(f"Available Game Memory Size: {int(info.available_game_memory / 1024 / 1024)} MB")
    print(f"Current Game CRC: {hex(info.current_game_crc)[2:]}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
