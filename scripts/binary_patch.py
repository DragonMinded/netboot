#! /usr/bin/env python3
import argparse
import os
import sys
from typing import List

from arcadeutils.binary import BinaryDiff


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Utilities for diffing or patching binary files.",
    )
    subparsers = parser.add_subparsers(help='commands', dest='command')

    # Parser for diffing two binary files
    diff_parser = subparsers.add_parser('diff', help='Diff two same-length binary files.')
    diff_parser.add_argument(
        'file1',
        metavar='FILE1',
        type=str,
        help='The base file that we will output diffs relative to.',
    )
    diff_parser.add_argument(
        'file2',
        metavar='FILE2',
        type=str,
        help='The file that we will compare against the base file to find diffs.',
    )
    diff_parser.add_argument(
        '--patch-file',
        metavar='FILE',
        type=str,
        help='Write patches to a file instead of stdout.',
    )

    # Parser for patching a binary file
    patch_parser = subparsers.add_parser('patch', help='Patch a binary file.')
    patch_parser.add_argument(
        'bin',
        metavar='BIN',
        type=str,
        help='The binary file we should patch.',
    )
    patch_parser.add_argument(
        'out',
        metavar='OUT',
        type=str,
        help='The file we should write the patched binary to.',
    )
    patch_parser.add_argument(
        '--patch-file',
        metavar='FILE',
        type=str,
        action='append',
        help=(
            'Read patches from a file instead of stdin. Can be specified multiple times '
            'to apply multiple patches. Patches will be applied in specified order.'
        ),
    )
    patch_parser.add_argument(
        '--reverse',
        action="store_true",
        help='Perform the patch in reverse.',
    )

    # Grab what we're doing
    args = parser.parse_args()

    if args.command == 'diff':
        with open(args.file1, "rb") as fpb:
            file1 = fpb.read()
        with open(args.file2, "rb") as fpb:
            file2 = fpb.read()

        try:
            differences = BinaryDiff.diff(file1, file2)
        except Exception as e:
            print(f"Could not diff {args.file1} against {args.file2}: {str(e)}", file=sys.stderr)
            return 1

        if not args.patch_file:
            for line in differences:
                print(line)
        else:
            with open(args.patch_file, "w") as fps:
                fps.write(os.linesep.join(differences) + os.linesep)
    elif args.command == 'patch':
        with open(args.bin, "rb") as fpb:
            data = fpb.read()

        patch_list: List[List[str]] = []
        if not args.patch_file:
            patch_list.append(sys.stdin.readlines())
        else:
            for patch in args.patch_file:
                with open(patch, "r") as fps:
                    patch_list.append(fps.readlines())

        for differences in patch_list:
            differences = [d.strip() for d in differences if d.strip()]

            try:
                data = BinaryDiff.patch(data, differences, reverse=args.reverse)
            except Exception as e:
                print(f"Could not patch {args.bin}: {str(e)}", file=sys.stderr)
                return 1

        with open(args.out, "wb") as fpb:
            fpb.write(data)

        print(f"Patched {args.bin} and wrote to {args.out}.")
    else:
        print("Please specify a valid command!", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
