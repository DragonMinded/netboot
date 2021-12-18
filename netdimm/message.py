#!/usr/bin/env python3
import struct
import time
import zlib
from typing import Dict, List, Optional

from netdimm import NetDimm, PeekPokeTypeEnum


MAX_PACKET_LENGTH: int = ((0xFF - 2) * 3)
MAX_READ_TIMEOUT: float = 2.0
MAX_EMPTY_READS: int = 10
MAX_FAILED_WRITES: int = 10
DATA_REGISTER: int = 0xC0DE10
SEND_STATUS_REGISTER: int = 0xC0DE20
RECV_STATUS_REGISTER: int = 0xC0DE30
CONFIG_REGISTER: int = 0xC0DE40
SCRATCH1_REGISTER: int = 0xC0DE50
SCRATCH2_REGISTER: int = 0xC0DE60

SEND_STATUS_REGISTER_SEED: int = 3
RECV_STATUS_REGISTER_SEED: int = 7
CONFIG_REGISTER_SEED: int = 19

CONFIG_MESSAGE_EXISTS: int = 0x00000001
CONFIG_MESSAGE_HAS_ZLIB: int = 0x00000002


def checksum_valid(data: int, seed: int) -> bool:
    sumval = (data & 0xFF) + ((data >> 8) & 0xFF) + ((data >> 16) & 0xFF) + seed
    return ((data >> 24) & 0xFF) == ((~sumval) & 0xFF)


def checksum_stamp(data: int, seed: int) -> int:
    sumval = (data & 0xFF) + ((data >> 8) & 0xFF) + ((data >> 16) & 0xFF) + seed
    return (((~sumval) & 0xFF) << 24) | (data & 0x00FFFFFF)


def read_scratch1_register(netdimm: NetDimm) -> Optional[int]:
    with netdimm.connection():
        return netdimm.peek(SCRATCH1_REGISTER, PeekPokeTypeEnum.TYPE_LONG)


def read_scratch2_register(netdimm: NetDimm) -> Optional[int]:
    with netdimm.connection():
        return netdimm.peek(SCRATCH2_REGISTER, PeekPokeTypeEnum.TYPE_LONG)


def write_scratch1_register(netdimm: NetDimm, value: int) -> None:
    with netdimm.connection():
        netdimm.poke(SCRATCH1_REGISTER, PeekPokeTypeEnum.TYPE_LONG, value)


def write_scratch2_register(netdimm: NetDimm, value: int) -> None:
    with netdimm.connection():
        netdimm.poke(SCRATCH2_REGISTER, PeekPokeTypeEnum.TYPE_LONG, value)


def read_send_status_register(netdimm: NetDimm) -> Optional[int]:
    with netdimm.connection():
        valid = False
        status: int = 0
        start = time.time()

        while not valid:
            status = 0
            while status == 0 or status == 0xFFFFFFFF and (time.time() - start <= MAX_READ_TIMEOUT):
                status = netdimm.peek(SEND_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG)

            valid = checksum_valid(status, SEND_STATUS_REGISTER_SEED)
            if not valid and (time.time() - start > MAX_READ_TIMEOUT):
                return None

        return status


def write_send_status_register(netdimm: NetDimm, value: int) -> None:
    with netdimm.connection():
        netdimm.poke(SEND_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG, checksum_stamp(value, SEND_STATUS_REGISTER_SEED))


def read_recv_status_register(netdimm: NetDimm) -> Optional[int]:
    with netdimm.connection():
        valid = False
        status: int = 0
        start = time.time()

        while not valid:
            status = 0
            while status == 0 or status == 0xFFFFFFFF and (time.time() - start <= MAX_READ_TIMEOUT):
                status = netdimm.peek(RECV_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG)

            valid = checksum_valid(status, RECV_STATUS_REGISTER_SEED)
            if not valid and (time.time() - start > MAX_READ_TIMEOUT):
                return None

        return status


def write_recv_status_register(netdimm: NetDimm, value: int) -> None:
    with netdimm.connection():
        netdimm.poke(RECV_STATUS_REGISTER, PeekPokeTypeEnum.TYPE_LONG, checksum_stamp(value, RECV_STATUS_REGISTER_SEED))


def read_config_register(netdimm: NetDimm) -> Optional[int]:
    with netdimm.connection():
        valid = False
        config: int = 0
        start = time.time()

        while not valid:
            config = 0
            while config == 0 or config == 0xFFFFFFFF and (time.time() - start <= MAX_READ_TIMEOUT):
                config = netdimm.peek(CONFIG_REGISTER, PeekPokeTypeEnum.TYPE_LONG)

            valid = checksum_valid(config, CONFIG_REGISTER_SEED)
            if not valid and (time.time() - start > MAX_READ_TIMEOUT):
                return None

        return config


def receive_packet(netdimm: NetDimm) -> Optional[bytes]:
    with netdimm.connection():
        # First, attempt to grab the next packet available.
        status = read_send_status_register(netdimm)
        if status is None:
            return None

        # Now, grab the length of the available packet.
        length = (status >> 12) & 0xFFF
        if length == 0:
            return None

        # Now, see if the transfer was partially done, if so rewind it.
        loc = status & 0xFFF
        if loc > 0:
            write_send_status_register(netdimm, 0)

        # Now, grab and assemble the data itself.
        data: List[Optional[int]] = [None] * length
        tries: int = 0
        while any(d is None for d in data):
            chunk = netdimm.peek(DATA_REGISTER, PeekPokeTypeEnum.TYPE_LONG)
            if ((chunk & 0xFF000000) >> 24) in {0x00, 0xFF}:
                tries += 1
                if tries > MAX_EMPTY_READS:
                    # We need to figure out where we left off.
                    for loc, val in enumerate(data):
                        if val is None:
                            # We found a spot to resume from.
                            write_send_status_register(netdimm, loc & 0xFFF)
                            tries = 0
                            break
                    else:
                        # We should always find a spot to resume from or there's an issue,
                        # since in this case we should be done.
                        raise Exception("Logic error!")
            else:
                # Grab the location for this chunk, stick the data in the right spot.
                location = (((chunk >> 24) & 0xFF) - 1) * 3

                for off, shift in enumerate([16, 8, 0]):
                    actual = off + location
                    if actual < length:
                        data[actual] = (chunk >> shift) & 0xFF

        # Grab the actual return data.
        bytedata = bytes([d for d in data if d is not None])
        if len(bytedata) != length:
            raise Exception("Logic error!")

        # Acknowledge the data transfer completed.
        write_send_status_register(netdimm, length & 0xFFF)

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
            if time.time() - start > MAX_READ_TIMEOUT:
                # Failed to request a new packet send in time.
                return False

            # First, attempt to see if there is any existing transfer in progress.
            status = read_recv_status_register(netdimm)
            if status is None:
                return False

            # Now, grab the length of the available packet.
            newlength = (status >> 12) & 0xFFF
            if newlength == 0:
                # Ready to start transferring!
                write_recv_status_register(netdimm, (length << 12) & 0xFFF000)
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
                chunk: int = ((((location // 3) + 1) << 24) & 0xFF000000)

                for shift in [16, 8, 0]:
                    if location < length:
                        chunk |= (data[location] & 0xFF) << shift
                        location += 1
                    else:
                        break

                # Send it.
                netdimm.poke(DATA_REGISTER, PeekPokeTypeEnum.TYPE_LONG, chunk)

            # Now, see if the data transfer was successful.
            status = read_recv_status_register(netdimm)
            if status is None:
                # Give up, we can't read from the status.
                return False

            # See if the packet was sent successfully. If not, then our location will
            # be set to where the target needs data sent from.
            newlength = (status >> 12) & 0xFFF
            location = status & 0xFFF

            if newlength == 0 and location == 0:
                # We succeeded! Time to exit
                return True
            elif newlength != length:
                raise Exception("Logic error!")


class Message:
    def __init__(self, msgid: int, data: bytes = b"") -> None:
        self.id = msgid
        self.data = data


class MessageException(Exception):
    pass


MESSAGE_HEADER_LENGTH: int = 8
MAX_MESSAGE_DATA_LENGTH: int = MAX_PACKET_LENGTH - MESSAGE_HEADER_LENGTH
MAX_MESSAGE_LENGTH: int = 0xFFFF
MAX_MESSAGE_TIMEOUT: float = 3.0


MESSAGE_HOST_STDOUT: int = 0x7FFE
MESSAGE_HOST_STDERR: int = 0x7FFF


send_sequence: int = 1


def send_message(netdimm: NetDimm, message: Message, verbose: bool = False) -> None:
    global send_sequence
    if send_sequence == 0:
        send_sequence = 1

    config = read_config_register(netdimm)
    if config is None:
        raise MessageException("Cannot read packetlib config register!")
    if (config & CONFIG_MESSAGE_EXISTS) == 0:
        raise MessageException("Host is not running message protocol!")
    zlib_enabled = (config & CONFIG_MESSAGE_HAS_ZLIB) != 0

    if verbose:
        print(f"Sending type: {hex(message.id)}, length: {len(message.data)}")

    data = message.data
    compressed = False
    if data and zlib_enabled:
        compresseddata = zlib.compress(data, level=9)
        if (len(compresseddata) + 4) < len(data):
            # Worth it to compress.
            data = struct.pack("<I", len(data)) + compresseddata
            compressed = True
            if verbose:
                print(f"Compressed {len(message.data)} down to {len(data)}")

    t = time.time()
    total_length = len(data)
    if total_length == 0:
        packetdata = struct.pack("<HHHH", (message.id & 0x7FFF) | (0x8000 if compressed else 0), send_sequence & 0xFFFF, 0, 0)
        if not send_packet(netdimm, packetdata):
            send_sequence = (send_sequence + 1) & 0xFFFF
            if verbose:
                print(f"Packet transfer failed in {time.time() - t} seconds")
            raise MessageException("Cannot send message!")
    else:
        location = 0
        for chunk in [data[i:(i + MAX_MESSAGE_DATA_LENGTH)] for i in range(0, total_length, MAX_MESSAGE_DATA_LENGTH)]:
            packetdata = struct.pack("<HHHH", (message.id & 0x7FFF) | (0x8000 if compressed else 0), send_sequence & 0xFFFF, total_length & 0xFFFF, location & 0xFFFF) + chunk
            location += len(chunk)

            if not send_packet(netdimm, packetdata):
                send_sequence = (send_sequence + 1) & 0xFFFF
                if verbose:
                    print(f"Packet transfer failed in {time.time() - t} seconds")
                raise MessageException("Cannot send message!")

    if verbose:
        print(f"Packet transfer took {time.time() - t} seconds")
    send_sequence = (send_sequence + 1) & 0xFFFF


pending_received_chunks: Dict[int, Dict[int, bytes]] = {}
pending_received_sizes: Dict[int, int] = {}
pending_received_msgids: Dict[int, int] = {}
pending_received_timestamp: Dict[int, float] = {}
recv_sequence: int = -1


def _packet_finished(sequence: int) -> bool:
    if sequence not in pending_received_sizes:
        return False
    total_length = pending_received_sizes[sequence]

    for needed_location in range(0, total_length, MAX_MESSAGE_DATA_LENGTH):
        if needed_location not in pending_received_chunks[sequence]:
            # We're missing this location.
            return False

    # We have all the bits
    return True


def receive_message(netdimm: NetDimm, verbose: bool = False) -> Optional[Message]:
    global recv_sequence

    config = read_config_register(netdimm)
    if config is None:
        return None
    if (config & CONFIG_MESSAGE_EXISTS) == 0:
        raise MessageException("Host is not running message protocol!")
    zlib_enabled = (config & CONFIG_MESSAGE_HAS_ZLIB) != 0

    # First, see if all packets are available for the current receive sequence.
    while True:
        if _packet_finished(recv_sequence):
            # We have a finished packet that we can receive!
            sequence = recv_sequence
            msgid = pending_received_msgids[sequence]
            total_length = pending_received_sizes[sequence]
            break
        elif recv_sequence > 1 and _packet_finished(1):
            # We wrapped our sequence number around, or the target restarted
            # and we want to resync with it here.
            recv_sequence = 1
            sequence = recv_sequence
            msgid = pending_received_msgids[sequence]
            total_length = pending_received_sizes[sequence]
            break
        elif recv_sequence in pending_received_timestamp and time.time() - pending_received_timestamp[recv_sequence] > MAX_MESSAGE_TIMEOUT:
            # Act like we just connected, force a resync.
            recv_sequence = -1

        # Try to receive a new packet.
        new_packet = receive_packet(netdimm)
        if new_packet is None:
            # First, if we don't know the received packet, we should grab the
            # lowest sequence we've received instead of exiting.
            if recv_sequence < 0:
                if pending_received_chunks:
                    potential_sequence = min(pending_received_chunks.keys())
                    next_potential_sequence = (potential_sequence + 1) & 0xFFFF
                    if next_potential_sequence == 0:
                        next_potential_sequence = 1

                    # The potential sequence could have been cut off, so if it is
                    # not entirely ready, discard it as we have no way in our
                    # protocol to signal that we need it fully retransmitted.
                    if potential_sequence in pending_received_chunks and not _packet_finished(potential_sequence) and _packet_finished(next_potential_sequence):
                        del pending_received_chunks[potential_sequence]
                        del pending_received_msgids[potential_sequence]
                        del pending_received_sizes[potential_sequence]
                        del pending_received_timestamp[potential_sequence]

                        # We know the next packet is ready, so just start there.
                        recv_sequence = next_potential_sequence
                        continue
                    elif _packet_finished(potential_sequence):
                        # This packet is actually ready, pop around the loop and
                        # reassemble it and then receive packets from there onward.
                        recv_sequence = potential_sequence
                        continue

            # No packets available, return that there isn't anything.
            return None

        # Make sure it isn't a dud packet.
        if len(new_packet) < MESSAGE_HEADER_LENGTH:
            raise MessageException("Got dud packet from target!")

        # See if this packet can be reassembled.
        msgid, sequence, total_length, location = struct.unpack("<HHHH", new_packet[0:8])

        if sequence not in pending_received_chunks:
            pending_received_chunks[sequence] = {}
            pending_received_msgids[sequence] = msgid
            pending_received_sizes[sequence] = total_length
            pending_received_timestamp[sequence] = time.time()

        if location not in pending_received_chunks[sequence]:
            pending_received_chunks[sequence][location] = new_packet[8:]
            pending_received_timestamp[sequence] = time.time()

    # We have it all!
    msgdata = b"".join(pending_received_chunks[sequence][position] for position in range(0, total_length, MAX_MESSAGE_DATA_LENGTH))
    del pending_received_chunks[sequence]
    del pending_received_msgids[sequence]
    del pending_received_sizes[sequence]
    del pending_received_timestamp[sequence]

    # Make sure we receive the next packet in order. We intentionally don't
    # wrap around the sequence here because we want to handle both the case
    # where the sequence number naturally wraps around as well as the case
    # of the host system rebooting and restarting its sequence in the same
    # code section above.
    recv_sequence += 1

    if zlib_enabled and (msgid & 0x8000 != 0):
        # It was compressed.
        if len(msgdata) >= 4:
            uncompressed_length = struct.unpack("<I", msgdata[0:4])[0]
            uncompressed_data = zlib.decompress(msgdata[4:])
            if len(uncompressed_data) != uncompressed_length:
                raise MessageException("Decompress error!")
            msgdata = uncompressed_data
            msgid = msgid & 0x7FFF
        else:
            raise MessageException("Decompress error!")

    if verbose:
        print(f"Received type: {hex(msgid)}, length: {len(msgdata)}")
    return Message(msgid, msgdata)
