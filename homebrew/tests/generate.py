#! /usr/bin/env python3
import argparse
import os
import sys
from typing import Dict, List, Set, Tuple


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Utility for generating the libNaomi test suite."
    )
    parser.add_argument(
        'file',
        metavar='FILE',
        type=str,
        nargs='+',
        help='The base address',
    )
    args = parser.parse_args()

    testsuite_files = [f for f in args.file if f == 'testsuite.c']
    output_files = [f for f in args.file if f.startswith("build/") and f.endswith(".c")]
    input_files = [f for f in args.file if f.startswith("test_") and f.endswith(".c")]

    if len(output_files) != 1:
        print("Could not determine output file from arguments!", file=sys.stderr)
        return 1
    if len(testsuite_files) != 1:
        print("Could not determine test suite file from arguments!", file=sys.stderr)
        return 1

    output_file = output_files[0]
    testsuite_file = testsuite_files[0]

    testfiles: List[str] = []
    tests: List[Tuple[str, str, int]] = []

    for filename in input_files:
        # Include this file.
        print(f"Looking at {filename} for tests...")

        with open(filename, "r") as fp:
            lines = fp.readlines()

        # First, discover test names.
        testnames: Set[str] = set()
        for line in lines:
            if "void test_" in line and "test_context_t" in line:
                _, testname = line.split("void", 1)
                testname, _ = testname.split("(", 1)
                testname = testname.strip()
                testnames.add(testname)

        if testnames:
            # Now, discover durations.
            testdurations: Dict[str, int] = {}
            for line in lines:
                if "#define " in line and "_duration" in line:
                    _, rest = line.split("#define", 1)
                    testname, value = rest.split("_duration")
                    testname = testname.strip()
                    value = value.strip()
                    if testname not in testnames:
                        print(f"Discovered duration for test {testname} but we didn't discover that test!")
                    else:
                        try:
                            testdurations[testname] = int(value)
                        except ValueError:
                            print(f"Discovered test duration for test {testname} has unparseable value {value}!")

            testfiles.append(filename)
            print(f"Tests discovered in {filename}:")
            for test in testnames:
                duration = testdurations.get(test, None)
                print(f"  {test} ({'no duration' if duration is None else duration})")
                tests.append((filename, test, duration or -1))
        else:
            print(f"Could not discover any tests in {filename}!")

    # Now, inject this into the original file and write that out.
    print(f"Generating {output_file} from tests and template file {testsuite_file}...")
    outputlines = []
    seenequals = 0
    section = ""

    with open(testsuite_file, "r") as fp:
        lines = fp.readlines()

    for line in lines:
        if line.startswith("//") and "=====" in line:
            seenequals += 1
            outputlines.append(line)
        else:
            if (seenequals & 1) == 1:
                # We're in a section
                section = line
                if "END " in section:
                    section = ""
            if (seenequals & 3) != 0x2:
                # We're inside a marked section
                outputlines.append(line)

        if (seenequals & 1) == 0:
            if "TEST FILES SECTION" in section:
                # This is where we spray our test files.
                for filename in testfiles:
                    outputlines.append(f'#include "{os.path.join("..", filename)}"{os.linesep}')
            elif "TEST CASES SECTION" in section:
                # This is where we spray our test cases.
                for i, (filename, name, duration) in enumerate(tests):
                    outputlines.append(f"    {{\"{filename}\", \"{name}\", {name}, {duration}}}{',' if i != (len(tests) - 1) else ''}{os.linesep}")
            section = ""

    with open(output_file, "w") as fp:
        fp.write("".join(outputlines))

    return 0


if __name__ == "__main__":
    sys.exit(main())
