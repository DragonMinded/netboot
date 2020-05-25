#! /usr/bin/env python3
import argparse
import sys

from naomi import NaomiRom, NaomiRomSection


def change(binfile: bytes, tochange: bytes, loc: int) -> bytes:
    return binfile[:loc] + tochange + binfile[(loc + len(tochange)):]


def main() -> int:
    # Create the argument parser
    parser = argparse.ArgumentParser(
        description="Utility for attaching a trojan to a commercial Naomi ROM.",
    )
    parser.add_argument(
        'bin',
        metavar='BIN',
        type=str,
        help='The binary file we should attach the trojan to.',
    )
    parser.add_argument(
        'exe',
        metavar='EXE',
        type=str,
        help='The executable binary blob we should attach to the end of the commercial ROM.',
    )
    parser.add_argument(
        '--offset',
        type=str,
        default='0x0c021000',
        help='Where to attach the springboard, in main memory. This is the hook for the trojan.',
    )
    parser.add_argument(
        '--output-file',
        metavar='BIN',
        type=str,
        help='A different file to output to instead of updating the binary specified directly.',
    )

    # Grab what we're doing
    args = parser.parse_args()

    # Grab the rom, parse it
    with open(args.bin, "rb") as fp:
        data = fp.read()
    naomi = NaomiRom(data)

    # Grab the attachment. This should be an executable binary blob. The easiest way to get
    # one of these is to compile a program using the toolchain in the homebrew/ directory,
    # and then copy out the naomi.bin file from the build/ directory and use that. Note that
    # in order to use this method, you will want to change the two executable start
    # addresses in naomi.ld to 0x0D000000 or any non-relative jumps will go to the wrong
    # code. Note also that if the game uses this address, you will run into trouble. You
    # might need to code a different springboard to a safe memory region if you run into
    # trouble using 0x0D000000.
    with open(args.exe, "rb") as fp:
        exe = fp.read()

    # Grab the springboard file. This was made by compiling the following
    # SH-4 code and then extracting the executable bytes.
    """
    .section .text
    .globl start

start:
    # Jump to location 0x0D000000
    mov #13,r0
    shll16 r0
    shll8 r0
    jmp @r0
    nop
    """
    springfile = bytes([0x0d, 0xe0, 0x28, 0x40, 0x18, 0x40, 0x2b, 0x40, 0x09, 0x00])

    # We need to add a EXE init section to the ROM
    executable = naomi.main_executable
    patchoffset = executable.sections[0].load_address - executable.sections[0].offset
    if len(executable.sections) >= 8:
        print("ROM already has the maximum number of init sections!", file=sys.stderr)
        return 1

    for sec in executable.sections:
        if sec.load_address == 0x0D000000:
            print("ROM already is trojan'd, cowardly giving up!", file=sys.stderr)
            return 1

    # Add a new section to the end of the rom for this binary data
    executable.sections.append(
        NaomiRomSection(
            offset=len(data),
            load_address=0x0D000000,
            length=len(exe),
        )
    )
    naomi.main_executable = executable

    # Now, just append it to the end of the file
    newdata = change(
        naomi.data + data[naomi.HEADER_LENGTH:] + exe,
        springfile,
        int(args.offset, 16) - patchoffset,
    )

    if args.output_file:
        print(f"Added trojan to the end of {args.output_file}.", file=sys.stderr)
        with open(args.output_file, "wb") as fp:
            fp.write(newdata)
    else:
        print(f"Added trojan to the end of {args.bin}.", file=sys.stderr)
        with open(args.bin, "wb") as fp:
            fp.write(newdata)

    return 0


if __name__ == "__main__":
    sys.exit(main())
