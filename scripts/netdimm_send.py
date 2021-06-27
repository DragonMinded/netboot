#!/usr/bin/env python3
# Triforce Netfirm Toolbox, put into the public domain.
# Please attribute properly, but only if you want.
import argparse
import sys
from netboot import BinaryDiff, NetDimm
from typing import Optional


def main() -> int:
    parser = argparse.ArgumentParser(description="Tools for sending images to NetDimm for Naomi/Chihiro/Triforce.")
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
        "--key",
        metavar="HEX",
        type=str,
        help="Key (as a 16 character hex string) to encrypt image file. Defaults to null key",
    )
    parser.add_argument(
        "--target",
        metavar="TARGET",
        type=str,
        help="Target platform this image is going to. Defaults to 'naomi', but 'chihiro' and 'triforce' are also valid",
    )
    parser.add_argument(
        "--version",
        metavar="VERSION",
        type=str,
        help="NetDimm firmware version this image is going to. Defaults to '3.01', but '1.07', '2.03' and '2.15' are also valid",
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

    args = parser.parse_args()

    # If the user specifies a key (not normally done), convert it
    key: Optional[bytes] = None
    if args.key:
        if len(args.key) != 16:
            raise Exception("Invalid key length for image!")
        key = bytes([int(args.key[x:(x + 2)], 16) for x in range(0, len(args.key), 2)])

    print("sending...", file=sys.stderr)
    netdimm = NetDimm(args.ip, target=args.target, version=args.version)

    # Grab the binary, patch it with requested patches.
    with open(args.image, "rb") as fp:
        data = fp.read()
    for patch in args.patch_file or []:
        with open(patch, "r") as pp:
            differences = pp.readlines()
        differences = [d.strip() for d in differences if d.strip()]
        try:
            data = BinaryDiff.patch(data, differences)
        except Exception as e:
            print(f"Could not patch {args.image}: {str(e)}", file=sys.stderr)
            return 1

    # Send the binary, reboot into the game.
    netdimm.send(data, key)
    print("rebooting into game...", file=sys.stderr)
    netdimm.reboot()
    print("ok!", file=sys.stderr)

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
