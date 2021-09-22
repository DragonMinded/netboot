#! /usr/bin/env python3
import argparse
import os
import os.path
import sys
import textwrap


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Utility for calculating start addresses."
    )
    parser.add_argument(
        'base',
        metavar='BASE',
        type=str,
        help='The base address',
    )
    parser.add_argument(
        'add',
        metavar='ADD',
        type=str,
        help='The amount to add',
    )
    args = parser.parse_args()

    base = int(args.base, 16) if '0x' in args.base.lower() else int(args.base, 10)
    add = int(args.add, 16) if '0x' in args.add.lower() else int(args.add, 10)

    print(hex(base + add))

    return 0


if __name__ == "__main__":
    sys.exit(main())
