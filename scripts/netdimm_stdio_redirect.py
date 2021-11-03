#!/usr/bin/env python3
import argparse
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
        '--verbose',
        action="store_true",
        help="Display verbose debugging information.",
    )

    args = parser.parse_args()
    verbose = args.verbose

    netdimm = NetDimm(args.ip, log=print)
    with netdimm.connection():
        while True:
            msg = receive_message(netdimm, verbose=verbose)
            if msg:
                if msg.id == MESSAGE_HOST_STDOUT:
                    print(msg.data.decode('utf-8'), end="")
                elif msg.id == MESSAGE_HOST_STDERR:
                    print(msg.data.decode('utf-8'), end="", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
