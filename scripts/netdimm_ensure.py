#!/usr/bin/env python3
# Triforce Netfirm Toolbox, put into the public domain.
# Please attribute properly, but only if you want.
import argparse
import sys
import time
import enum
from netboot import Cabinet, CabinetStateEnum, CabinetRegionEnum, TargetEnum
from netdimm import NetDimmVersionEnum
from typing import Any, Optional


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
    parser = argparse.ArgumentParser(description="Tools for guaranteeing a single image get sent to a Naomi/Chihiro/Triforce NetDimm.")
    parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm is configured on.",
    )
    parser.add_argument(
        "image",
        metavar="IMAGE",
        type=str,
        help="The image file we should send to the NetDimm.",
    )
    parser.add_argument(
        "--target",
        metavar="TARGET",
        type=TargetEnum,
        action=EnumAction,
        default=TargetEnum.TARGET_NAOMI,
        help="Target platform this image is going to. Defaults to 'naomi'. Choose from 'naomi', 'chihiro' or 'triforce'.",
    )
    parser.add_argument(
        "--version",
        metavar="VERSION",
        type=NetDimmVersionEnum,
        action=EnumAction,
        default=NetDimmVersionEnum.VERSION_4_01,
        help="NetDimm firmware version this image is going to. Defaults to '4.01'. Choose from '1.02', '2.06', '2.17', '3.03', '3.17', '4.01' or '4.02'.",
    )
    parser.add_argument(
        '--patch-file',
        metavar='FILE',
        type=str,
        action='append',
        help=(
            'Patch to apply to image on-the-fly while sending to the NetDimm. '
            'Can be specified multiple times to apply multiple patches. '
            'Patches will be applied in specified order. If not specified, the '
            'image is sent without patching.'
        ),
    )
    parser.add_argument(
        '--settings-file',
        metavar='FILE',
        type=str,
        help=(
            'Settings to apply to image on-the-fly while sending to the NetDimm. '
            'Currently only supported for the Naomi platform. For Naomi, the '
            'settings file should be a valid 128-byte EEPROM file as obtained '
            'from an emulator or as created using the "edit_settings" utility, '
            'or a 32-kbyte SRAM file as obtained from an emulator.'
        ),
    )

    args = parser.parse_args()

    settings: Optional[bytes] = None
    if args.settings_file:
        with open(args.settings_file, "rb") as fp:
            settings = fp.read()

    print(f"managing {args.ip} to ensure {args.image} is always loaded")
    cabinet = Cabinet(
        args.ip,
        CabinetRegionEnum.REGION_UNKNOWN,
        "No description.",
        args.image,
        {args.image: args.patch_file or []},
        {args.image: settings},
        target=args.target,
        version=args.version,
        enabled=True,
        quiet=True,
    )
    while True:
        # Tick the state machine, display progress
        cabinet.tick()
        status, progress = cabinet.state

        if status == CabinetStateEnum.STATE_STARTUP:
            print("starting up...        \r", end="")
        elif status == CabinetStateEnum.STATE_DISABLED:
            print("cabinet disabled...   \r", end="")
        elif status == CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_ON:
            print("waiting for cabinet...\r", end="")
        elif status == CabinetStateEnum.STATE_WAIT_FOR_CABINET_POWER_OFF:
            print("running game...       \r", end="")
        elif status == CabinetStateEnum.STATE_SEND_CURRENT_GAME:
            print(f"sending ({progress}%)...       \r", end="")
        elif status == CabinetStateEnum.STATE_CHECK_CURRENT_GAME:
            print("verifying game crc... \r", end="")
        else:
            raise Exception(f"Unknown status {status}")

        # Don't overwhelm the CPU
        time.sleep(1)

    return 0


if __name__ == "__main__":
    sys.exit(main())

# ok, now you're on your own, the tools are there.
# We see the DIMM space as it's seen by the dimm-board (i.e. as on the disc).
# It will be transparently decrypted when accessed from Host, unless a
# zero-key has been set. We do this before uploading something, so we don't
# have to bother with the inserted key chip. Still, some key chip must be
# present.
# You need to configure the triforce to boot in "satellite mode",
# which can be done using the dipswitches on the board (type-3) or jumpers
# (VxWorks-style).
# The dipswitch for type-3 must be in the following position:
#       - SW1: ON ON *
#       - It shouldn't wait for a GDROM anymore, but display error 31.
# For the VxWorks-Style:
#       - Locate JP1..JP3 on the upper board in the DIMM board. They are near
#               the GDROM-connector.
#               The jumpers must be in this position for satellite mode:
#               1               3
#               [. .].  JP1
#               [. .].  JP2
#                .[. .] JP3
#       - when you switch on the triforce, it should say "waiting for network..."
#
# Good Luck. Warez are evil.
