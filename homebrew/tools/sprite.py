#! /usr/bin/env python3
import argparse
import io
import os
import os.path
import struct
import sys
import textwrap
from PIL import Image  # type: ignore
from typing import List


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Utility for converting image files to C include style sprites."
    )
    parser.add_argument(
        'c',
        metavar='C_FILE',
        type=str,
        help='The C file we should generate.',
    )
    parser.add_argument(
        'img',
        metavar='IMG',
        type=str,
        help='The image file we should generate a C file for.',
    )
    parser.add_argument(
        '--depth',
        metavar='DEPTH',
        type=int,
        help='The depth of the final sprite, in bytes. Should match the video mode you are initializing.',
    )
    args = parser.parse_args()

    # Read the image, get the dimensions.
    with open(args.img, "rb") as bfp:
        data = bfp.read()
    texture = Image.open(io.BytesIO(data))
    width, height = texture.size

    # Convert it to RGBA, convert the data to a format that Naomi knows.
    pixels = texture.convert('RGBA')
    outdata: List[bytes] = []

    if args.depth == 2:
        for r, g, b, a in pixels.getdata():
            outdata.append(struct.pack("<H", ((b >> 3) & (0x1F << 0)) | ((g << 2) & (0x1F << 5)) | ((r << 7) & (0x1F << 10)) | ((a << 8) & 0x8000)))
    else:
        # TODO: Add this when we support other depths.
        raise Exception(f"Unsupported depth {args.depth}!")

    bindata = b"".join(outdata)
    name = os.path.basename(args.img).replace('.', '_')
    cfile = f"""
    #include <stdint.h>

    uint8_t __{name}_data[{len(bindata)}] __attribute__ ((aligned (4))) = {{
        {", ".join(hex(b) for b in bindata)}
    }};
    unsigned int {name}_width = {width};
    unsigned int {name}_height = {height};
    void *{name}_data = __{name}_data;
    """

    with open(args.c, "w") as sfp:
        sfp.write(textwrap.dedent(cfile))

    return 0


if __name__ == "__main__":
    sys.exit(main())
