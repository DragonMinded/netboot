import argparse
import math
import sys


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Utility for generating the pitch register table C source.",
    )
    parser.add_argument(
        'c',
        metavar='C_FILE',
        type=str,
        help='The C file we should generate.',
    )
    parser.add_argument(
        'table_size',
        metavar='SIZE',
        type=int,
        help='The table jump size (64, 128, 256 or 512).',
    )
    args = parser.parse_args()

    if args.table_size == 64:
        TABLE_JUMP_SIZE = 64
        INDEX_SHIFT = 6
        FRAC_MASK = 0x3F
    elif args.table_size == 128:
        TABLE_JUMP_SIZE = 128
        INDEX_SHIFT = 7
        FRAC_MASK = 0x7F
    elif args.table_size == 256:
        TABLE_JUMP_SIZE = 256
        INDEX_SHIFT = 8
        FRAC_MASK = 0xFF
    elif args.table_size == 512:
        TABLE_JUMP_SIZE = 512
        INDEX_SHIFT = 9
        FRAC_MASK = 0x1FF
    else:
        print("Invalid table size selection!", file=sys.stderr)
        return 1

    IMPORTANT_FREQUENCIES = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000}

    # Actual cents calculation.
    def cents(x: int) -> int:
        return int(1200 * math.log2(x / 44100))

    # Start with frequency "0", since this is invalid in a log2.
    table = [0]
    for i in range(TABLE_JUMP_SIZE, 96000 + (2 * TABLE_JUMP_SIZE), TABLE_JUMP_SIZE):
        table.append(cents(i))

    # Define the approx function.
    def centsapprox(x: int) -> int:
        index = x >> INDEX_SHIFT
        low = table[index]
        high = table[index + 1]
        return low + (((high - low) * (x & FRAC_MASK)) >> INDEX_SHIFT)

    # Now, calculate error
    totalerror = 0
    worsterror = 0
    for i in range(8000, 96001):
        error = cents(i) - centsapprox(i)
        totalerror += abs(error)
        worsterror = max(abs(error), worsterror)

        if i in IMPORTANT_FREQUENCIES and error != 0:
            print(f"Frequency {i} has error {error}!")

    print(f"Total memory is {len(table) * 2} bytes")
    print(f"Total error is {totalerror} cents")
    print(f"Worst error is {worsterror} cents")

    # Now, calculate cent translation table.
    fns = [round(((2 ** (i / 1200)) - 1) * 2**10) for i in range(1200)]

    # Now, generate a header file for this
    print(f"Generating {args.c} with LUT step size {args.table_size}.")
    with open(args.c, "w") as fp:
        def p(s: str) -> None:
            print(s, file=fp)

        # Solely for alignment reasons.
        def p_(s: str) -> None:
            p(s)

        p_("#include <stdint.h>")
        p_("")
        p(f"int16_t centtable[{len(table)}] = {{")

        for chunk in [table[x:(x + 16)] for x in range(0, len(table), 16)]:
            p_("    " + ", ".join([str(x) for x in chunk]) + ", ")
        p_("};")
        p_("")
        p(f"uint16_t fnstable[{len(fns)}] = {{")
        for chunk in [fns[x:(x + 16)] for x in range(0, len(fns), 16)]:
            p_("    " + ", ".join([str(x) for x in chunk]) + ", ")
        p_("};")
        p_("")
        p_("uint32_t pitch_reg(unsigned int samplerate)")
        p_("{")
        p_("    // Calculate cents difference from 44100.")
        p(f"    unsigned int index = samplerate >> {INDEX_SHIFT};")
        p_("    int low = centtable[index];")
        p_("    int high = centtable[index + 1];")
        p(f"    int cents = low + (((high - low) * (samplerate & {FRAC_MASK})) >> {INDEX_SHIFT});")
        p_("")
        p_("    // Calcualte octaves from cents.")
        p_("    int octave = 0;")
        p_("    while (cents < 0)")
        p_("    {")
        p_("        cents += 1200;")
        p_("        octave -= 1;")
        p_("    }")
        p_("    while (cents >= 1200)")
        p_("    {")
        p_("        cents -= 1200;")
        p_("        octave += 1;")
        p_("    }")
        p_("")
        p_("    // Finally, generate the register contents.")
        p_("    return ((octave & 0xF) << 11) | fnstable[cents];")
        p_("}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
