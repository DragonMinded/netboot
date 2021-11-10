#! /usr/bin/env python3
import argparse
import os
import os.path
import struct
import sys
from typing import Dict, Optional, Tuple


ROMFS_MAJOR: int = 1
ROMFS_MINOR: int = 0

ROMFS_TYPE_DIR: int = 1
ROMFS_TYPE_FILE: int = 2


class Entry:
    def __init__(self, offset: int, type: int, size: int) -> None:
        self.offset = offset
        self.type = type
        self.size = size


def make_directory(directory: str, parent: Optional[int], parententries: Optional[int]) -> Tuple[int, bytes]:
    print(f"Adding {directory} to ROM FS!")

    # First, calculate the size of the directory structure.
    fnames = list(os.listdir(directory))
    headerlen = (len(fnames) + 2) * 256

    # Now, start the header structure itself.
    files: Dict[str, Entry] = {
        # Pointer to self entry.
        '.': Entry(0, ROMFS_TYPE_DIR, len(fnames) + 2),
        # Pointer to parent, or self if no parent.
        '..': Entry(parent if parent is not None else 0, ROMFS_TYPE_DIR, parententries if parententries is not None else (len(fnames) + 2)),
    }

    # Now, calculate the data.
    data: bytes = b""
    datalen = 0

    for fname in fnames:
        path = os.path.join(directory, fname)

        if os.path.isfile(path):
            # Regular file.
            with open(path, "rb") as bfp:
                filedata = bfp.read()

            filelen = len(filedata)
            origlen = filelen

            # Pad to 4-byte boundary. We only need two but whatever.
            while filelen % 4 != 0:
                filedata += b"\0"
                filelen += 1

            # Add it to the entries, add the data to the actual data.
            print(f"Added {fname} to ROM FS!")
            files[fname] = Entry(headerlen + datalen, ROMFS_TYPE_FILE, origlen)
            data += filedata
            datalen += filelen
        elif os.path.isdir(path):
            # Directory. Make the directory and link it back to ourselves.
            numentries, dirdata = make_directory(path, -(headerlen + datalen), len(fnames) + 2)
            dirlen = len(dirdata)

            files[fname] = Entry(headerlen + datalen, ROMFS_TYPE_DIR, numentries)
            data += dirdata
            datalen += dirlen
        else:
            # Don't know what this one is!
            print(f"Skipping {fname} as it is not a file or a directory!")

    # Now, generate the header!
    header = b""
    for fname, entry in files.items():
        headerentry = struct.pack("<III", entry.offset, entry.size, entry.type)
        filename = fname.encode("utf-8")[:(256 - 12)]
        if len(filename) < (256 - 12):
            filename = filename + b"\0" * ((256 - 12) - len(filename))

        header += headerentry + filename

    if len(header) != headerlen:
        raise Exception("Logic error!")

    print(f"Added {directory} with {len(files)} entries to ROM FS!")
    return len(files), header + data


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Utility for generating a ROM FS that can be attached to a ROM."
    )
    parser.add_argument(
        'romfs',
        metavar='ROMFS',
        type=str,
        help='The ROMFS file we should write to after generating the data.',
    )
    parser.add_argument(
        'dir',
        metavar='DIR',
        type=str,
        help='The directory we should treat as the root of the ROM FS.',
    )
    args = parser.parse_args()

    # Start with the header.
    header = b"ROMFS\0" + struct.pack("<BBI", ROMFS_MAJOR, ROMFS_MINOR, 0x11291985)

    # Add on the root directory.
    rootentries, data = make_directory(os.path.abspath(args.dir), None, None)

    # Read the image, get the dimensions.
    with open(args.romfs, "wb") as bfp:
        bfp.write(header + struct.pack("<I", rootentries) + data)

    return 0


if __name__ == "__main__":
    sys.exit(main())
