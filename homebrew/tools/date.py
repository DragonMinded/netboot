#! /usr/bin/env python3
import argparse
import datetime
import sys


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Utility for calculating build date."
    )
    args = parser.parse_args()

    today = datetime.datetime.now()
    print(today.strftime("%Y%m%d"))

    return 0


if __name__ == "__main__":
    sys.exit(main())
