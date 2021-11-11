#!/usr/bin/env python3
# Triforce Netfirm Toolbox, put into the public domain.
# Please attribute properly, but only if you want.
import argparse
import sys

from netdimm import NetDimm, PeekPokeTypeEnum


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Tools for peeking/poking values into running memory on a Naomi/Triforce/Chihiro.",
    )
    subparsers = parser.add_subparsers(help='Action to take', dest='action')

    peek_parser = subparsers.add_parser(
        'peek',
        help='Peek at a value in an 8/16/32-bit memory location',
        description='Peek at a value in an 8/16/32-bit memory location',
    )
    peek_parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm is configured on.",
    )
    peek_parser.add_argument(
        "address",
        metavar="ADDR",
        type=str,
        help="The hex address of memory that you would like to peek into.",
    )
    peek_parser.add_argument(
        "size",
        metavar="SIZE",
        type=int,
        help="The size in bytes you want to read. Valid values are 1, 2 and 4.",
    )

    poke_parser = subparsers.add_parser(
        'poke',
        help='Poke a value into an 8/16/32-bit memory location',
        description='Poke a value into an 8/16/32-bit memory location',
    )
    poke_parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm is configured on.",
    )
    poke_parser.add_argument(
        "address",
        metavar="ADDR",
        type=str,
        help="The hex address of memory that you would like to poke into.",
    )
    poke_parser.add_argument(
        "size",
        metavar="SIZE",
        type=int,
        help="The size in bytes you want to write. Valid values are 1, 2 and 4.",
    )
    poke_parser.add_argument(
        "data",
        metavar="VALUE",
        type=str,
        help="The hex value you wish to write into the address.",
    )

    args = parser.parse_args()
    netdimm = NetDimm(args.ip)

    if args.action == "peek":
        if args.size == 1:
            data = netdimm.peek(int(args.address, 16), PeekPokeTypeEnum.TYPE_BYTE) & 0xFF
        elif args.size == 2:
            data = netdimm.peek(int(args.address, 16), PeekPokeTypeEnum.TYPE_SHORT) & 0xFFFF
        elif args.size == 4:
            data = netdimm.peek(int(args.address, 16), PeekPokeTypeEnum.TYPE_LONG) & 0xFFFFFFFF
        else:
            raise Exception(f"Invalid size selection {args.size}!")

        hexdata = hex(data)[2:]
        while len(hexdata) < (2 * args.size):
            hexdata = "0" + hexdata
        print(hexdata)

    elif args.action == "poke":
        if args.size == 1:
            netdimm.poke(int(args.address, 16), PeekPokeTypeEnum.TYPE_BYTE, int(args.data, 16) & 0xFF)
        elif args.size == 2:
            netdimm.poke(int(args.address, 16), PeekPokeTypeEnum.TYPE_SHORT, int(args.data, 16) & 0xFFFF)
        elif args.size == 4:
            netdimm.poke(int(args.address, 16), PeekPokeTypeEnum.TYPE_LONG, int(args.data, 16) & 0xFFFFFFFF)
        else:
            raise Exception(f"Invalid size selection {args.size}!")

    return 0


if __name__ == "__main__":
    sys.exit(main())
