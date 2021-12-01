#!/usr/bin/env python3
import argparse
import os
import re
import select
import socket
import struct
import sys
from typing import Optional, Tuple

from netdimm import NetDimm, PeekPokeTypeEnum, receive_message, MESSAGE_HOST_STDOUT, MESSAGE_HOST_STDERR
from naomi import NaomiRom


def gdb_strip_ack(data: bytes) -> Tuple[Optional[bool], bytes]:
    if data[0:1] == b'-':
        return False, data[1:]
    if data[0:1] == b'+':
        return True, data[1:]
    return None, data


def gdb_check_crc(data: bytes) -> Tuple[Optional[bytes], bool]:
    if data[0:1] == b'$':
        # Split the packet to its data and CRC.
        packet, crc = data[1:].rsplit(b'#', 1)
        if len(crc) != 2:
            return None, False

        # Calculate packet CRC.
        crcint = int(crc, 16)
        checksum = 0
        for byte in packet:
            checksum += byte
        checksum = checksum % 256

        # Verify the CRC itself.
        if checksum == crcint:
            return packet, True
        else:
            return packet, False
    else:
        return None, False


def _hex(val: int) -> bytes:
    hexval = hex(val)[2:]
    while len(hexval) < 2:
        hexval = "0" + hexval
    return hexval.encode('ascii')


def gdb_make_crc(packet: bytes) -> bytes:
    checksum = 0
    for byte in packet:
        checksum += byte
    checksum = checksum % 256
    return b"$" + packet + b"#" + _hex(checksum)


def target_make_crc(addr: int) -> int:
    addr = addr & 0x00FFFFFF
    crc = ~(((addr & 0xFF) + ((addr >> 8) & 0xFF) + ((addr >> 16) & 0xFF)) & 0xFF)
    return ((crc << 24) & 0xFF000000) | addr


def target_validate_crc(addr: int) -> Optional[int]:
    crc = ~(((addr & 0xFF) + ((addr >> 8) & 0xFF) + ((addr >> 16) & 0xFF)) & 0xFF)
    if ((addr >> 24) & 0xFF) == (crc & 0xFF):
        return addr & 0x00FFFFFF
    else:
        return None


def gdb_handle_packet(netdimm: NetDimm, knock_address: int, ringbuffer_address: int, packet: bytes) -> Tuple[bool, Optional[bytes]]:
    if packet[:11] == b"qSupported:":
        supported_options = [x.decode('ascii') for x in packet[11:].split(b";")]  # noqa

        # For now, only reply with the max packet size. In the future we need
        # to advertise what we do support.
        return True, b"PacketSize=512"

    if packet == b"qSymbol::":
        # Symbol lookup, we don't need to handle this.
        return True, b"OK"

    if packet == b"vMustReplyEmpty":
        # GDB is probing us to see if we handle this correctly. We must return
        # an empty string, as the packet states.
        return True, b""

    if packet[0:1] in {b"h", b"H", b"q", b"Q", b"g", b"G", b"m", b"M", b"?"}:
        # Packet that should be handled by the Naomi. First, lay it down the packet
        # itself so it can be read by the target.
        netdimm.send_chunk(ringbuffer_address, struct.pack("<I", len(packet)) + packet)

        # Now, generate an interrupt on the target to handle the packet.
        netdimm.poke(knock_address, PeekPokeTypeEnum.TYPE_LONG, target_make_crc(ringbuffer_address))

        # Now, grab the location of the response.
        loc = None
        while loc is None:
            loc = target_validate_crc(netdimm.peek(knock_address, PeekPokeTypeEnum.TYPE_LONG))

        # Now, read the response itself.
        valid, length = struct.unpack("<II", netdimm.receive_chunk(loc, 8))
        if valid != 0 and length > 0:
            return True, netdimm.receive_chunk(loc + 8, length)
        elif valid != 0:
            return True, b""
        else:
            return False, None

    return True, None


def main() -> int:
    parser = argparse.ArgumentParser(description="Host a GDB remote server for on-target debugging.")
    parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm is configured on.",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=2345,
        help="The port to listen on for GDB connections. Defaults to %(default)s.",
    )
    parser.add_argument(
        '--enable-stdio-hooks',
        action="store_true",
        help="Also receive redirected stdout/stderr from the Naomi binary.",
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
        # First, establish where the GDB buffers should be placed on the
        # remote target. Round the end of the ROM to a multiple of 4 for
        # ease of access from the target.
        info = netdimm.info()
        buffer_loc = (info.current_game_size + 3) & 0xFFFFFFFFC

        # Read the ROM header to determine where the entrypoint address is.
        # We use this to write our ringbuffer address and to generate interrupts
        # on the target for single-stepping and program breaks.
        headerdata = netdimm.receive_chunk(0, NaomiRom.HEADER_LENGTH)
        header = NaomiRom(headerdata)
        knock_loc = header.main_executable.entrypoint

        if verbose:
            print(f"IRQ location is {hex(knock_loc)}{os.linesep}Comms ringbuffer is at {hex(buffer_loc)}")

        # Now, listen for GDB.
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.bind(('', args.port))
        server_socket.listen(1)

        if verbose:
            print(f"Listening for GDB remote connections on port {args.port}")

        sockets = [server_socket]
        client_socket = None

        while True:
            # See if we have any incoming connections to handle.
            readable, _, _ = select.select(sockets, [], [], 0)
            for sock in readable:
                if sock is server_socket:
                    client_socket, address = server_socket.accept()
                    sockets.append(client_socket)
                else:
                    data = sock.recv(1024)
                    if data:
                        is_ack, data = gdb_strip_ack(data)
                        while is_ack is not None:
                            # See if we got any ack we weren't expecting.
                            is_ack, data = gdb_strip_ack(data)

                        if data:
                            # Handle GDB protocol here.
                            packet, valid = gdb_check_crc(data)
                            if valid:
                                if packet is None:
                                    raise Exception("Logic error!")

                                if verbose:
                                    print(f"Got packet \"{packet.decode('latin1')}\"")

                                valid, response = gdb_handle_packet(netdimm, knock_loc, buffer_loc, packet)
                                if valid:
                                    if response is not None:
                                        if verbose:
                                            print(f"Sending packet \"{response.decode('latin1')}\"")
                                        sock.send(b'+' + gdb_make_crc(response))
                                    else:
                                        if verbose:
                                            print("Sending positive acknowledgement")
                                        sock.send(b'+')
                                else:
                                    if verbose:
                                        print("Sending negative acknowledgement")
                                    sock.send(b'-')
                            else:
                                if verbose:
                                    print("Got packet with invalid CRC!")
                                sock.send(b"-")
                    else:
                        sock.close()
                        sockets.remove(sock)

            # Process any incoming stdio/stderr message from the target.
            if args.enable_stdio_hooks:
                msg = receive_message(netdimm, verbose=verbose)
                if msg:
                    if msg.id == MESSAGE_HOST_STDOUT:
                        print(sanitize(msg.data), end="")
                    elif msg.id == MESSAGE_HOST_STDERR:
                        print(sanitize(msg.data), end="", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
