#!/usr/bin/env python3
import argparse
import os
import sys

from arcadeutils import FileBytes
from netdimm import NetDimm, NetDimmException, Message, send_message, receive_message, MESSAGE_HOST_STDOUT, MESSAGE_HOST_STDERR


# The root of the repo.
root = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), ".."))


MESSAGE_READY: int = 0x2000
MESSAGE_SRAM_READ_REQUEST: int = 0x2001
MESSAGE_SRAM_WRITE_REQUEST: int = 0x2002
MESSAGE_SRAM_READ: int = 0x2003
MESSAGE_SRAM_WRITE: int = 0x2004
MESSAGE_DONE: int = 0x2005


def main() -> int:
    parser = argparse.ArgumentParser(description="Utility for dumping/restoring an SRAM file on a SEGA Naomi.")
    subparsers = parser.add_subparsers(help='Action to take', dest='action')

    dump_parser = subparsers.add_parser(
        'dump',
        help='Dump the SRAM from a SEGA Naomi and write it to a file.',
        description='Dump the SRAM from a SEGA Naomi and write it to a file.',
    )
    dump_parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm we should use is configured on.",
    )
    dump_parser.add_argument(
        'sram',
        metavar='SRAM',
        type=str,
        help='The SRAM file we should write the contents of the SRAM to.',
    )
    dump_parser.add_argument(
        '--exe',
        metavar='EXE',
        type=str,
        default=os.path.join(root, 'homebrew', 'sramdump', 'sramdump.bin'),
        help='The helper executable that we should send to dump/restore SRAM files on the Naomi. Defaults to %(default)s.',
    )
    dump_parser.add_argument(
        '--verbose',
        action="store_true",
        help="Display verbose debugging information.",
    )

    restore_parser = subparsers.add_parser(
        'restore',
        help='Restore the SRAM on a SEGA Naomi from a file.',
        description='Restore the SRAM on a SEGA Naomi from a file.',
    )
    restore_parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm we should use is configured on.",
    )
    restore_parser.add_argument(
        'sram',
        metavar='SRAM',
        type=str,
        help='The SRAM file we should read the contents of the SRAM from.',
    )
    restore_parser.add_argument(
        '--exe',
        metavar='EXE',
        type=str,
        default=os.path.join(root, 'homebrew', 'sramdump', 'sramdump.bin'),
        help='The helper executable that we should send to dump/restore SRAM files on the Naomi. Defaults to %(default)s.',
    )
    restore_parser.add_argument(
        '--verbose',
        action="store_true",
        help="Display verbose debugging information.",
    )

    args = parser.parse_args()
    verbose = args.verbose

    if args.action == "dump":
        netdimm = NetDimm(args.ip, log=print if verbose else None)

        with open(args.exe, "rb") as fp:
            helperdata = FileBytes(fp)
            try:
                # Now, connect to the net dimm, send the menu and then start communicating with it.
                print("Connecting to net dimm...")
                print("Sending helper to net dimm...")
                netdimm.send(helperdata, disable_crc_check=True)
                netdimm.reboot()
                print("Waiting for Naomi to boot...")
            except NetDimmException:
                # Mark failure so we don't try to communicate below.
                print("Sending helper executable failed!")
                return 1

        try:
            with netdimm.connection():
                while True:
                    msg = receive_message(netdimm, verbose=verbose)
                    if not msg:
                        continue
                    if msg.id == MESSAGE_READY:
                        print("Dumping SRAM...")
                        send_message(netdimm, Message(MESSAGE_SRAM_READ_REQUEST), verbose=verbose)
                    elif msg.id == MESSAGE_SRAM_READ:
                        if len(msg.data) != 0x8000:
                            print("Got wrong size for SRAM!")
                            return 1
                        else:
                            with open(args.sram, "wb") as fp:
                                fp.write(msg.data)
                            print(f"Wrote SRAM from Naomi to '{args.sram}'.")
                            send_message(netdimm, Message(MESSAGE_DONE), verbose=verbose)
                            break
                    elif msg.id == MESSAGE_HOST_STDOUT:
                        print(msg.data.decode('utf-8'), end="")
                    elif msg.id == MESSAGE_HOST_STDERR:
                        print(msg.data.decode('utf-8'), end="", file=sys.stderr)
                    else:
                        print("Got unexpected packet!")
                        return 1
        except NetDimmException:
            # Mark failure so we don't try to wait for power down below.
            print("Communicating with the helper failed!")
            return 1
    if args.action == "restore":
        netdimm = NetDimm(args.ip, log=print if verbose else None)

        with open(args.exe, "rb") as fp:
            helperdata = FileBytes(fp)
            try:
                # Now, connect to the net dimm, send the menu and then start communicating with it.
                print("Connecting to net dimm...")
                print("Sending helper to net dimm...")
                netdimm.send(helperdata, disable_crc_check=True)
                netdimm.reboot()
                print("Waiting for Naomi to boot...")
            except NetDimmException:
                # Mark failure so we don't try to communicate below.
                print("Sending helper executable failed!")
                return 1

        try:
            with netdimm.connection():
                while True:
                    msg = receive_message(netdimm, verbose=verbose)
                    if not msg:
                        continue
                    if msg.id == MESSAGE_READY:
                        send_message(netdimm, Message(MESSAGE_SRAM_WRITE_REQUEST), verbose=verbose)

                        print("Restoring SRAM...")
                        with open(args.sram, "rb") as fp:
                            data = fp.read()
                        send_message(netdimm, Message(MESSAGE_SRAM_WRITE, data), verbose=verbose)
                        send_message(netdimm, Message(MESSAGE_DONE), verbose=verbose)
                        break
                    elif msg.id == MESSAGE_HOST_STDOUT:
                        print(msg.data.decode('utf-8'), end="")
                    elif msg.id == MESSAGE_HOST_STDERR:
                        print(msg.data.decode('utf-8'), end="", file=sys.stderr)
                    else:
                        print("Got unexpected packet!")
                        return 1
        except NetDimmException:
            # Mark failure so we don't try to wait for power down below.
            print("Communicating with the helper failed!")
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
