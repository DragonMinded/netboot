#!/usr/bin/env python3
# Triforce Netfirm Toolbox, put into the public domain.
# Please attribute properly, but only if you want.
import argparse
import struct
import sys
import time
from netboot import NetDimm, PeekPokeTypeEnum
from typing import Dict, List, Optional


MAX_PACKET_LENGTH: int = 253
MAX_EMPTY_READS: int = 10
MAX_FAILED_WRITES: int = 10
MENU_DATA_REGISTER: int = 0xC0DE10
MENU_SEND_STATUS_REGISTER: int = 0xC0DE20
MENU_RECV_STATUS_REGISTER: int = 0xC0DE30


def checksum_valid(data: int) -> bool:
    sumval = (data & 0xFF) + ((data >> 8) & 0xFF)
    return ((data >> 24) & 0xFF) == 0 and ((data >> 16) & 0xFF) == ((~sumval) & 0xFF)


def checksum_stamp(data: int) -> int:
    sumval = (data & 0xFF) + ((data >> 8) & 0xFF)
    return (((~sumval) & 0xFF) << 16) | (data & 0x0000FFFF)


def read_send_status_register(netdimm: NetDimm) -> Optional[int]:
    with netdimm.connection():
        valid = False
        status: int = 0
        start = time.time()

        while not valid:
            while status == 0 or status == 0xFFFFFFFF:
                status = netdimm.peek(MENU_SEND_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG)

            valid = checksum_valid(status)

            if not valid and (time.time() - start > 1.0):
                return None

        return status


def write_send_status_register(netdimm: NetDimm, value: int) -> None:
    with netdimm.connection():
        netdimm.poke(MENU_SEND_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG, checksum_stamp(value))


def read_recv_status_register(netdimm: NetDimm) -> Optional[int]:
    with netdimm.connection():
        valid = False
        status: int = 0
        start = time.time()

        while not valid:
            while status == 0 or status == 0xFFFFFFFF:
                status = netdimm.peek(MENU_RECV_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG)

            valid = checksum_valid(status)

            if not valid and (time.time() - start > 1.0):
                return None

        return status


def write_recv_status_register(netdimm: NetDimm, value: int) -> None:
    with netdimm.connection():
        netdimm.poke(MENU_RECV_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG, checksum_stamp(value))


def receive_packet(netdimm: NetDimm) -> Optional[bytes]:
    with netdimm.connection():
        # First, attempt to grab the next packet available.
        status = read_send_status_register(netdimm)
        if status is None:
            return None

        # Now, grab the length of the available packet.
        length = (status >> 8) & 0xFF
        if length == 0:
            return None

        # Now, see if the transfer was partially done, if so rewind it.
        loc = status & 0xFF
        if loc > 0:
            write_send_status_register(netdimm, 0)

        # Now, grab and assemble the data itself.
        data: List[Optional[int]] = [None] * length
        tries: int = 0
        while any([d is None for d in data]):
            chunk = netdimm.peek(MENU_DATA_REGISTER, PeekPokeTypeEnum.TYPE_LONG)
            if ((chunk & 0xFF000000) >> 24) in {0x00, 0xFF}:
                tries += 1
                if tries > MAX_EMPTY_READS:
                    # We need to figure out where we left off.
                    for loc, val in enumerate(data):
                        if val is None:
                            # We found a spot to resume from.
                            write_send_status_register(netdimm, loc & 0xFF)
                            tries = 0
                            break
                    else:
                        # We should always find a spot to resume from or there's an issue,
                        # since in this case we should be done.
                        raise Exception("Logic error!")
            else:
                # Grab the location for this chunk, stick the data in the right spot.
                location = ((chunk >> 24) & 0xFF) - 1

                for off, shift in enumerate([16, 8, 0]):
                    actual = off + location
                    if actual < length:
                        data[actual] = (chunk >> shift) & 0xFF

        # Grab the actual return data.
        bytedata = bytes([d for d in data if d is not None])
        if len(bytedata) != length:
            raise Exception("Logic error!")

        # Acknowledge the data transfer completed.
        write_send_status_register(netdimm, length & 0xFF)

        # Return the actual data!
        return bytedata


def send_packet(netdimm: NetDimm, data: bytes) -> bool:
    length = len(data)
    if length > MAX_PACKET_LENGTH:
        raise Exception("Packet is too long to send!")

    with netdimm.connection():
        start = time.time()
        sent_length = False
        while True:
            if time.time() - start > 1.0:
                # Failed to request a new packet send in time.
                return False

            # First, attempt to see if there is any existing transfer in progress.
            status = read_recv_status_register(netdimm)
            if status is None:
                return False

            # Now, grab the length of the available packet.
            newlength = (status >> 8) & 0xFF
            if newlength == 0:
                # Ready to start transferring!
                write_recv_status_register(netdimm, (length << 8) & 0xFF00)
                sent_length = True
            elif sent_length is False or newlength != length:
                # Cancel old transfer.
                write_recv_status_register(netdimm, 0)
                sent_length = False
            elif newlength == length:
                # Ready to send data.
                break
            else:
                # Shouldn't be possible.
                raise Exception("Logic error!")

        # Now set the current transfer location. This can be rewound by the target
        # if it failed to receive all of the data.
        location = 0
        while True:
            while location < length:
                # Sum up the next amount of data, up to 3 bytes.
                chunk: int = (((location + 1) << 24) & 0xFF000000)

                for shift in [16, 8, 0]:
                    if location < length:
                        chunk |= (data[location] & 0xFF) << shift
                        location += 1
                    else:
                        break

                # Send it.
                netdimm.poke(MENU_DATA_REGISTER, PeekPokeTypeEnum.TYPE_LONG, chunk)

            # Now, see if the data transfer was successful.
            status = read_recv_status_register(netdimm)
            if status is None:
                # Give up, we can't read from the status.
                return False

            # See if the packet was sent successfully. If not, then our location will
            # be set to where the target needs data sent from.
            newlength = (status >> 8) & 0xFF
            location = status & 0xFF

            if newlength == 0 and location == 0:
                # We succeeded! Time to exit
                return True
            elif newlength != length:
                raise Exception("Logic error!")


class Message:
    def __init__(self, msgid: int, data: bytes = b"") -> None:
        self.id = msgid
        self.data = data


MESSAGE_HEADER_LENGTH: int = 8
MAX_MESSAGE_DATA_LENGTH: int = MAX_PACKET_LENGTH - MESSAGE_HEADER_LENGTH


send_sequence: int = 1


def send_message(netdimm: NetDimm, message: Message) -> bool:
    global send_sequence
    if send_sequence == 0:
        send_sequence = 1

    total_length = len(message.data)
    if total_length == 0:
        packetdata = struct.pack("<HHHH", message.id & 0xFFFF, send_sequence & 0xFFFF, 0, 0)
        if not send_packet(netdimm, packetdata):
            send_sequence = (send_sequence + 1) & 0xFFFF
            return False
    else:
        location = 0
        for chunk in [message.data[i:(i + MAX_MESSAGE_DATA_LENGTH)] for i in range(0, total_length, MAX_MESSAGE_DATA_LENGTH)]:
            packetdata = struct.pack("<HHHH", message.id & 0xFFFF, send_sequence & 0xFFFF, total_length & 0xFFFF, location & 0xFFFF) + chunk
            location += len(chunk)

            if not send_packet(netdimm, packetdata):
                send_sequence = (send_sequence + 1) & 0xFFFF
                return False

    send_sequence = (send_sequence + 1) & 0xFFFF
    return True


pending_received_chunks: Dict[int, Dict[int, bytes]] = {}


def receive_message(netdimm: NetDimm) -> Optional[Message]:
    # Try to receive a new packet.
    new_packet = receive_packet(netdimm)
    if new_packet is None:
        return None

    # Make sure it isn't a dud packet.
    if len(new_packet) < MESSAGE_HEADER_LENGTH:
        return None

    # See if this packet can be reassembled.
    msgid, sequence, total_length, location = struct.unpack("<HHHH", new_packet[0:8])

    if sequence not in pending_received_chunks:
        pending_received_chunks[sequence] = {}
    if location not in pending_received_chunks[sequence]:
        pending_received_chunks[sequence][location] = new_packet[8:]

    for needed_location in range(0, total_length, MAX_MESSAGE_DATA_LENGTH):
        if needed_location not in pending_received_chunks[sequence]:
            # We're missing this location.
            return None

    # We have it all!
    msgdata = b"".join(pending_received_chunks[sequence][position] for position in range(0, total_length, MAX_MESSAGE_DATA_LENGTH))
    del pending_received_chunks[sequence]

    return Message(msgid, msgdata)


def main() -> int:
    parser = argparse.ArgumentParser(description="Provide an on-target menu for selecting games. Currently only works with Naomi.")
    parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm is configured on.",
    )

    args = parser.parse_args()

    netdimm = NetDimm(args.ip)
    if not send_message(netdimm, Message(0x1234, b"Four" + b"\0" * 1020)):
        print("Failed to send!")
        return 1

    while True:
        msg = receive_message(netdimm)
        if msg:
            print(f"Received type: {hex(msg.id)}, length: {len(msg.data)}, {msg.data.decode('ascii')}")
            send_message(netdimm, Message(0xC0DE))

    return 0


if __name__ == "__main__":
    sys.exit(main())
