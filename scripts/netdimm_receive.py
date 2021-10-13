#!/usr/bin/env python3
# Triforce Netfirm Toolbox, put into the public domain.
# Please attribute properly, but only if you want.
import argparse
import enum
import sys
from netboot import NetDimm, TargetEnum, NetDimmVersionEnum
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
    parser = argparse.ArgumentParser(description="Tools for receiving images from NetDimm for Naomi/Chihiro/Triforce.")
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
        help="The image file we should write after receiving from to the NetDimm.",
    )
    parser.add_argument(
        "--target",
        metavar="TARGET",
        type=TargetEnum,
        action=EnumAction,
        default=TargetEnum.TARGET_NAOMI,
        help="Target platform this image is coming from. Defaults to 'naomi'. Choose from 'naomi', 'chihiro' or 'triforce'.",
    )
    parser.add_argument(
        "--version",
        metavar="VERSION",
        type=NetDimmVersionEnum,
        action=EnumAction,
        default=NetDimmVersionEnum.VERSION_4_01,
        help="NetDimm firmware version this image is coming from. Defaults to '4.01'. Choose from '1.02', '2.06', '2.17', '3.03', '3.17', '4.01' or '4.02'.",
    )

    args = parser.parse_args()

    print("receiving...", file=sys.stderr)
    netdimm = NetDimm(args.ip, version=args.version)

    # Receive the binary.
    data = netdimm.receive()

    if data:
        with open(args.image, "wb") as fp:
            fp.write(data)

        print("ok!", file=sys.stderr)
    else:
        print("no valid game exists on net dimm!", file=sys.stderr)

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
