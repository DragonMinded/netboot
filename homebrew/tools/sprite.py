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
        description="Utility for converting image files to C include style or raw sprites."
    )
    parser.add_argument(
        'file',
        metavar='FILE',
        type=str,
        help='The output file we should generate.',
    )
    parser.add_argument(
        'img',
        metavar='IMG',
        type=str,
        help='The image file we should use to generate the output file.',
    )
    parser.add_argument(
        '--mode',
        metavar='MODE',
        type=str,
        help='The mode of the final sprite. Should match the video mode you are initializing. Options include "RGBA1555", "RGBA8888" and "INTENSITY8".',
    )
    parser.add_argument(
        '--raw',
        action="store_true",
        help='Output a raw sprite file instead of a C include file.',
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
    mode: str = args.mode.lower()

    if mode == "intensity8":
        for r, g, b, _ in pixels.getdata():
            gray = int(0.2989 * r + 0.5870 * g + 0.1140 * b)
            outdata.append(struct.pack("<B", gray))
    elif mode == "rgba1555":
        for r, g, b, a in pixels.getdata():
            outdata.append(struct.pack("<H", ((b >> 3) & (0x1F << 0)) | ((g << 2) & (0x1F << 5)) | ((r << 7) & (0x1F << 10)) | ((a << 8) & 0x8000)))
    elif mode == "rgba4444":
        for r, g, b, a in pixels.getdata():
            outdata.append(struct.pack("<H", ((b >> 4) & 0xFF) | (g & 0xF0) | ((r << 4) & 0xF00) | ((a << 8) & 0xF000)))
    elif mode == "rgba8888":
        for r, g, b, a in pixels.getdata():
            outdata.append(struct.pack("<I", ((b & 0xFF) << 0) | ((g & 0xFF) << 8) | ((r & 0xFF) << 16) | ((a & 0xFF) << 24)))
    else:
        # TODO: Add this when we support other modes.
        raise Exception(f"Unsupported depth {args.mode}!")

    bindata = b"".join(outdata)

    if args.raw:
        with open(args.file, "wb") as bfp:
            bfp.write(bindata)
    else:
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

        with open(args.file, "w") as sfp:
            sfp.write(textwrap.dedent(cfile))

    return 0


if __name__ == "__main__":
    sys.exit(main())
