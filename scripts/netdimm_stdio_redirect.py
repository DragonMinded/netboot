#!/usr/bin/env python3
import argparse
import re
import sys

from netdimm import NetDimm, receive_message, MESSAGE_HOST_STDOUT, MESSAGE_HOST_STDERR


def main() -> int:
    parser = argparse.ArgumentParser(description="Receive redirected stdio/stderr from a Naomi binary running libnaomimessage.")
    parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm is configured on.",
    )
    parser.add_argument(
        '--strip-vt100-commands',
        action="store_true",
        help="Strip out VT-100 escape commands for terminals that do not support them.",
    )
    parser.add_argument(
        '--verbose',
        action="store_true",
        help="Display verbose debugging information.",
    )

    args = parser.parse_args()
    verbose = args.verbose

    if args.strip_vt100_commands:
        ansi_escape_8bit = re.compile(
            br'(?:\x1B[@-Z\\-_]|[\x80-\x9A\x9C-\x9F]|(?:\x1B\[|\x9B)[0-?]*[ -/]*[@-~])'
        )

        def sanitize(string: bytes) -> str:
            return ansi_escape_8bit.sub(b'', string).decode('utf-8')
    else:
        def sanitize(string: bytes) -> str:
            return string.decode('utf-8')

    netdimm = NetDimm(args.ip, log=print)
    with netdimm.connection():
        while True:
            msg = receive_message(netdimm, verbose=verbose)
            if msg:
                if msg.id == MESSAGE_HOST_STDOUT:
                    print(sanitize(msg.data), end="")
                elif msg.id == MESSAGE_HOST_STDERR:
                    print(sanitize(msg.data), end="", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
