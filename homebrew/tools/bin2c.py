#! /usr/bin/env python3
import argparse
import os
import os.path
import sys
import textwrap


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Utility for converting binary files to C includes."
    )
    parser.add_argument(
        'c',
        metavar='C_FILE',
        type=str,
        help='The C file we should generate.',
    )
    parser.add_argument(
        'bin',
        metavar='BIN',
        type=str,
        help='The binary file we should generate a C file for.',
    )
    args = parser.parse_args()
    with open(args.bin, "rb") as bfp:
        data = bfp.read()

    name = os.path.basename(args.bin).replace('.', '_')
    cfile = f"""
    #include <stdint.h>

    uint8_t __{name}_data[{len(data)}] __attribute__ ((aligned (4))) = {{
        {", ".join(hex(b) for b in data)}
    }};
    unsigned int {name}_len = {len(data)};
    uint8_t *{name}_data = __{name}_data;
    """

    with open(args.c, "w") as sfp:
        sfp.write(textwrap.dedent(cfile))

    return 0


if __name__ == "__main__":
    sys.exit(main())
