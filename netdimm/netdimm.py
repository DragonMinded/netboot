#!/usr/bin/env python3
# Triforce Netfirm Toolbox, put into the public domain.
# Please attribute properly, but only if you want.
import os
import sys
import socket
import struct
import time
import zlib
from Crypto.Cipher import DES
from contextlib import contextmanager
from enum import Enum
from typing import Any, Callable, Dict, Generator, List, Optional, Union, cast

from arcadeutils import FileBytes


class NetDimmException(Exception):
    pass


class NetDimmVersionEnum(Enum):
    VERSION_UNKNOWN = "UNKNOWN"
    VERSION_1_02 = "1.02"
    VERSION_2_03 = "2.03"
    VERSION_2_06 = "2.06"
    VERSION_2_13 = "2.13"
    VERSION_2_17 = "2.17"
    VERSION_3_01 = "3.01"
    VERSION_3_03 = "3.03"
    VERSION_3_12 = "3.12"
    VERSION_3_17 = "3.17"
    VERSION_4_01 = "4.01"
    VERSION_4_02 = "4.02"


class NetDimmTargetEnum(Enum):
    TARGET_UNKNOWN = "unknown"
    TARGET_CHIHIRO = "chihiro"
    TARGET_NAOMI = "naomi"
    TARGET_TRIFORCE = "triforce"


class CRCStatusEnum(Enum):
    STATUS_CHECKING = 1
    STATUS_VALID = 2
    STATUS_INVALID = 3
    STATUS_BAD_MEMORY = 4
    STATUS_DISABLED = 5


class PeekPokeTypeEnum(Enum):
    # Retrieve a single byte.
    TYPE_BYTE = 1
    # Retrieve a short (two bytes).
    TYPE_SHORT = 2
    # Retrieve a long (4 bytes).
    TYPE_LONG = 3


class NetDimmInfo:
    def __init__(
        self,
        current_game_crc: int,
        current_game_size: int,
        game_crc_status: CRCStatusEnum,
        memory_size: int,
        firmware_version: NetDimmVersionEnum,
        available_game_memory: int,
        control_address: int,
    ) -> None:
        self.current_game_crc = current_game_crc
        self.current_game_size = current_game_size
        self.game_crc_status = game_crc_status
        self.memory_size = memory_size
        self.firmware_version = firmware_version
        self.available_game_memory = available_game_memory
        self.control_address = control_address


class NetDimmPacket:
    def __init__(self, pktid: int, flags: int, data: bytes = b'') -> None:
        self.pktid = pktid
        self.flags = flags
        self.data = data

    @property
    def length(self) -> int:
        return len(self.data)


class NetDimm:
    DEFAULT_TIMEOUTS: Dict[NetDimmTargetEnum, int] = {
        NetDimmTargetEnum.TARGET_UNKNOWN: 15,
        NetDimmTargetEnum.TARGET_NAOMI: 15,
        NetDimmTargetEnum.TARGET_CHIHIRO: 40,
        NetDimmTargetEnum.TARGET_TRIFORCE: 40,
    }

    @staticmethod
    def crc(data: Union[bytes, FileBytes]) -> int:
        crc: int = 0
        if isinstance(data, bytes):
            crc = zlib.crc32(data, crc)
        elif isinstance(data, FileBytes):
            # Do this in chunks so we don't accidentally load the whole file.
            for offset in range(0, len(data), 0x8000):
                crc = zlib.crc32(data[offset:(offset + 0x8000)], crc)
        else:
            raise Exception("Logic error!")
        return (~crc) & 0xFFFFFFFF

    def __init__(
        self,
        ip: str,
        version: Optional[NetDimmVersionEnum] = None,
        target: Optional[NetDimmTargetEnum] = None,
        log: Optional[Callable[..., Any]] = None,
        timeout: Optional[int] = None,
    ) -> None:
        self.ip: str = ip
        self.sock: Optional[socket.socket] = None
        self.log: Optional[Callable[..., Any]] = log
        self.version: NetDimmVersionEnum = version or NetDimmVersionEnum.VERSION_UNKNOWN
        self.target: NetDimmTargetEnum = target or NetDimmTargetEnum.TARGET_UNKNOWN

        # A sane default for different targets, at least in my testing, is 10 seconds. However, OSX
        # users and some people have reported that this is too fast. So, 15 seconds it is for everything.
        # User reports for Chihiro are that it is super slow, so it needs a longer timeout. I have no
        # idea on Triforce so I set it the same as Chihiro.
        default_timeout = NetDimm.DEFAULT_TIMEOUTS[self.target]
        if timeout is None:
            try:
                timeout = int(os.environ.get('NETDIMM_TIMEOUT_SECONDS') or default_timeout)
            except Exception:
                timeout = default_timeout
        self.timeout: int = timeout

    def __repr__(self) -> str:
        return f"NetDimm(ip={repr(self.ip)}, version={repr(self.version)}, target={repr(self.target)}, timeout={repr(self.timeout)})"

    def info(self) -> NetDimmInfo:
        with self.connection():
            # Ask for DIMM firmware info and such.
            info = self.__get_information()

            # Update our own system information based on the returned version.
            self.version = info.firmware_version
            return info

    def send(
        self,
        data: Union[bytes, FileBytes],
        key: Optional[bytes] = None,
        disable_crc_check: bool = False,
        disable_now_loading: bool = False,
        progress_callback: Optional[Callable[[int, int], None]] = None,
    ) -> None:
        with self.connection():
            # First, signal back to calling code that we've started
            if progress_callback:
                progress_callback(0, len(data))

            if not disable_now_loading:
                # Reboot and display "now loading..." on the cabinet screen
                self.__set_host_mode(1)

                # The official sega transfer tool update sleeps for 5 seconds
                # here, presumably because the net dimm doesn't respond to
                # packets while it is ressetting the target to display "now
                # loading". However, our timeout is 10 seconds so this ends
                # up working without that sleep in practice.

            if key:
                # Send the key that we're going to use to encrypt
                self.__set_key_code(key)
            else:
                # disable encryption by setting magic zero-key
                self.__set_key_code(b"\x00" * 8)

            if disable_crc_check:
                self.__disable_crc_check()
            else:
                self.__enable_crc_check()

            # uploads file. Also sets "dimm information" (file length and crc32)
            self.__upload_file(data, key, progress_callback or (lambda _cur, _tot: None))

    def receive(self, progress_callback: Optional[Callable[[int, int], None]] = None) -> Optional[bytes]:
        with self.connection():
            info = self.__get_information()

            if info.game_crc_status in {CRCStatusEnum.STATUS_VALID, CRCStatusEnum.STATUS_DISABLED} and info.current_game_size > 0:
                # First, signal back to calling code that we've started
                if progress_callback:
                    progress_callback(0, info.current_game_size)

                data: List[bytes] = []
                address: int = 0

                while address < info.current_game_size:
                    # Display progress if we're in CLI mode.
                    self.__print("%08x %d%%\r" % (address, int(float(address * 100) / float(info.current_game_size))), newline=False)

                    # Get next chunk size.
                    amount = info.current_game_size - address
                    if amount > 0x8000:
                        amount = 0x8000

                    # Get next chunk.
                    chunk = self.__download(address, amount)
                    data.append(chunk)
                    address += len(chunk)

                    if progress_callback:
                        progress_callback(address, info.current_game_size)

                return b''.join(data)
            else:
                return None

    def send_chunk(self, offset: int, data: Union[bytes, FileBytes]) -> None:
        with self.connection():
            addr: int = 0
            total: int = len(data)
            sequence: int = 1

            while addr < total:
                # Upload data to a particular address.
                current = data[addr:(addr + 0x8000)]
                curlen = len(current)
                last_packet = addr + curlen == total

                self.__upload(sequence, offset + addr, current, last_packet)
                addr += curlen
                sequence += 1

    def receive_chunk(self, offset: int, length: int) -> bytes:
        with self.connection():
            data: List[bytes] = []
            address: int = 0

            while address < length:
                # Get next chunk size.
                amount = length - address
                if amount > 0x8000:
                    amount = 0x8000

                # Get next chunk.
                chunk = self.__download(offset + address, amount)
                data.append(chunk)
                address += len(chunk)

            return b''.join(data)

    def reboot(self) -> None:
        with self.connection():
            # restart host, this wil boot into game
            self.__restart()

            # set time limit to 10 minutes.
            self.__set_time_limit(10)

    def set_time_limit(self, limit: int) -> None:
        with self.connection():
            # set time limit to specified number of minutes.
            self.__set_time_limit(limit)

    def wipe_current_game(self) -> None:
        with self.connection():
            # Wipe the CRC information and file size of the current game, so that on next
            # reboot the cabinet will not attempt to CRC check or boot the game.
            self.__upload(1, 0xffff0000, b"\0" * 32, False)

    def peek(self, addr: int, type: PeekPokeTypeEnum) -> int:
        with self.connection():
            return self.__host_peek(addr, type)

    def poke(self, addr: int, type: PeekPokeTypeEnum, data: int) -> None:
        with self.connection():
            self.__host_poke(addr, data, type)

    def __print(self, string: str, newline: bool = True) -> None:
        if self.log is not None:
            try:
                self.log(string, newline=newline)
            except TypeError:
                self.log(string, end=os.linesep if newline else "")

    def __read(self, num: int) -> bytes:
        if self.sock is None:
            raise NetDimmException("Not connected to NetDimm")

        try:
            # a function to receive a number of bytes with hard blocking
            res: List[bytes] = []
            left: int = num
            start = time.time()

            while left > 0:
                if time.time() - start > 10.0:
                    raise NetDimmException("Could not receive data from NetDimm")
                ret = self.sock.recv(left)
                left -= len(ret)
                res.append(ret)

            return b"".join(res)
        except Exception as e:
            raise NetDimmException("Could not receive data from NetDimm") from e

    @contextmanager
    def connection(self) -> Generator[None, None, None]:
        if self.sock is not None:
            # We are already connected!
            yield
            return

        # connect to the net dimm. Port is tcp/10703.
        # note that this port is only open on
        # - all Type-3 triforces,
        # - pre-type3 triforces jumpered to satellite mode.
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

            # OSX can be a bit difficult to deal with. It appears that their handling of
            # socket timeouts differs enough from Linux-based systems that we need our
            # own code path. Specifically, I don't think that it respects timeouts set
            # after a connection has been made. Without this, OSX-based systems will
            # time out when trying to reboot and send a game. Note that this behavior
            # has been seen on some niche Linux-based OSes as well, so I am including an
            # environment-based escape hatch to enable/disable this just in case. If you
            # are on OSX the alternate timeout handling will be automatically enabled unless
            # you set the 'DEFAULT_TIMEOUT_HANDLING' environment variable. If you are not
            # on OSX and you set 'ALTERNATE_TIMEOUT_HANDLING' the alternate will be enabled
            # as well.
            if (sys.platform == 'darwin' or os.environ.get('ALTERNATE_TIMEOUT_HANDLING')) and (not os.environ.get('DEFAULT_TIMEOUT_HANDLING')):
                self.sock.settimeout(self.timeout)
                self.sock.setblocking(True)
                self.sock.connect((self.ip, 10703))
            else:
                self.sock.settimeout(1)
                self.sock.connect((self.ip, 10703))
                self.sock.settimeout(self.timeout)

        except Exception as e:
            raise NetDimmException("Could not connect to NetDimm") from e

        try:
            # Sending this packet is not strictly necessary, but transfergame.exe
            # sends it. It maps to a NOP packet at least on 3.17 firmware but
            # having the net dimm accept it is a good indication that you are talking
            # to an actual net dimm and not some random thing listening on port 10703.
            self.__startup()

            yield
        finally:
            self.sock.close()
            self.sock = None

    # Both requests and responses follow this header with length data bytes
    # after. Some have the ability to send/receive variable length (like send/recv
    # dimm packets) and some require a specific length or they do not return.
    #
    # Header words are packed as little-endian bytes and are as such: AABBCCCC
    # AA -   Packet type. Any of 256 values, but in practice most are unrecognized.
    # BB -   Seems to be some sort of flags and flow control. Packets have been observed
    #        with 00, 80 and 81 in practice. The bottom bit signifies, as best as I can
    #        tell, that the host intends to send more of the same packet type. It set to
    #        0 for dimm send requests except for the last packet. The top bit I have not
    #        figured out.
    # CCCC - Length of the data in bytes that follows this header, not including the 4
    #        header bytes.
    def __send_packet(self, packet: NetDimmPacket) -> None:
        if self.sock is None:
            raise NetDimmException("Not connected to NetDimm")
        try:
            self.sock.send(
                struct.pack(
                    "<I",
                    (
                        ((packet.pktid & 0xFF) << 24) |  # noqa: W504
                        ((packet.flags & 0xFF) << 16) |  # noqa: W504
                        (packet.length & 0xFFFF)
                    ),
                ) + packet.data
            )
        except Exception as e:
            raise NetDimmException("Could not send data to NetDimm") from e

    def __recv_packet(self) -> NetDimmPacket:
        # First read the header to get the packet length.
        header = self.__read(4)

        # Construct a structure to represent this packet, minus any optional data.
        headerbytes = struct.unpack("<I", header)[0]
        packet = NetDimmPacket(
            (headerbytes >> 24) & 0xFF,
            (headerbytes >> 16) & 0xFF,
        )
        length = headerbytes & 0xFFFF

        # Read optional data.
        if length > 0:
            data = self.__read(length)
            packet.data = data

        # Return the parsed packet.
        return packet

    def __startup(self) -> None:
        # This is mapped to a NOOP packet. At least in older versions of net dimm. I don't
        # know why transfergame.exe sends this, maybe older versions of net dimm firmware need it?
        self.__send_packet(NetDimmPacket(0x01, 0x00))

    def __validate_address(self, addr: int, type: PeekPokeTypeEnum) -> None:
        if type == PeekPokeTypeEnum.TYPE_BYTE:
            # Any address can be read as a byte.
            return
        if type == PeekPokeTypeEnum.TYPE_SHORT:
            # Only even addresses can be read as a short, without
            # returning bogus data.
            if addr & 0x1 != 0:
                raise NetDimmException("Cannot have misaligned address for peek/poke and SHORT data type!")
            return
        if type == PeekPokeTypeEnum.TYPE_LONG:
            # Only even addresses can be read as a short, without
            # returning bogus data.
            if addr & 0x3 != 0:
                raise NetDimmException("Cannot have misaligned address for peek/poke and LONG data type!")
            return
        raise Exception("Logic error!")

    def __host_peek(self, addr: int, type: PeekPokeTypeEnum) -> int:
        # Type appears to be the type of data being requested, where the valid values are 1-3 and
        # they correspond to byte, short and long respectively. It appears thet there is code in
        # firmware 3.17 that checks against 0 as well. At least in net dimm firmware 3.17 there is
        # some functionality gated around the target system not being naomi, so it looks like the
        # implementation might be different on naomi versus triforce/chihiro and might have different
        # capabilities. It appears that at least on naomi, the addresses must be properly aligned
        # to their size. So byte can have any address, short must not be on odd addresses and long
        # must be on multiples of 4.
        self.__validate_address(addr, type)
        self.__send_packet(NetDimmPacket(0x10, 0x00, struct.pack("<II", addr, type.value)))
        response = self.__recv_packet()
        if response.pktid != 0x10:
            raise NetDimmException("Unexpected data returned from peek4 packet!")
        if response.length != 8:
            raise NetDimmException("Unexpected data length returned from peek4 packet!")
        _success, val = struct.unpack("<II", response.data)
        return cast(int, val)

    def __host_poke(self, addr: int, data: int, type: PeekPokeTypeEnum) -> None:
        # Same type comment as the above peek4 command. Same caveats about naomi systems in particular.
        self.__validate_address(addr, type)
        self.__send_packet(NetDimmPacket(0x11, 0x00, struct.pack("<III", addr, type.value, data)))

    def __host_control_read(self) -> int:
        # Read the control data location from the host that the net dimm is plugged into.
        self.__send_packet(NetDimmPacket(0x16, 0x00))
        response = self.__recv_packet()
        if response.pktid != 0x10:
            # Yes, its buggy for this as well, and they reused the peek ID.
            raise NetDimmException("Unexpected data returned from control read packet!")
        if response.length != 8:
            raise NetDimmException("Unexpected data length returned from control read packet!")
        _success, val = struct.unpack("<II", response.data)
        return cast(int, val)

    def __exchange_host_mode(self, mask_bits: int, set_bits: int) -> int:
        self.__send_packet(NetDimmPacket(0x07, 0x00, struct.pack("<I", ((mask_bits & 0xFF) << 8) | (set_bits & 0xFF))))

        # Set mode returns the resulting mode, after the original mode is or'd with "set_bits"
        # and and'd with "mask_bits". I guess this allows you to see the final mode with your
        # additions and subtractions since it might not be what you expect. It also allows you
        # to query the current mode by sending a packet with mask bits 0xff and set bits 0x0.
        response = self.__recv_packet()
        if response.pktid != 0x07:
            raise NetDimmException("Unexpected data returned from set host mode packet!")
        if response.length != 4:
            raise NetDimmException("Unexpected data length returned from set host mode packet!")

        # The top 3 bytes are not set to anything, only the bottom byte is set to the combined mode.
        return cast(int, struct.unpack("<I", response.data)[0] & 0xFF)

    def __set_host_mode(self, mode: int) -> None:
        self.__exchange_host_mode(0, mode)

    def __get_host_mode(self) -> int:
        # The following modes have been observed:
        #
        # 0 - System is in "CHECKING NETWORK"/"CHECKING MEMORY"/running game mode.
        # 1 - System was requested to display "NOW LOADING..." and is rebooting into that mode.
        # 2 - System is in "NOW LOADING..." mode but no transfer has been initiated.
        # 10 - System is in "NOW LOADING..." mode and a transfer has been initiated, rebooting naomi before continuing.
        # 20 - System is in "NOW LIADING..." mode and a transfer is continuing.
        return self.__exchange_host_mode(0xFF, 0)

    def __exchange_dimm_mode(self, mask_bits: int, set_bits: int) -> int:
        self.__send_packet(NetDimmPacket(0x08, 0x00, struct.pack("<I", ((mask_bits & 0xFF) << 8) | (set_bits & 0xFF))))

        # Set mode returns the resulting mode, after the original mode is or'd with "set_bits"
        # and and'd with "mask_bits". I guess this allows you to see the final mode with your
        # additions and subtractions since it might not be what you expect. It also allows you
        # to query the current mode by sending a packet with mask bits 0xff and set bits 0x0.
        response = self.__recv_packet()
        if response.pktid != 0x08:
            raise NetDimmException("Unexpected data returned from set dimm mode packet!")
        if response.length != 4:
            raise NetDimmException("Unexpected data length returned from set dimm mode packet!")

        # The top 3 bytes are not set to anything, only the bottom byte is set to the combined mode.
        return cast(int, struct.unpack("<I", response.data)[0] & 0xFF)

    def __set_dimm_mode(self, mode: int) -> None:
        # Absolutely no idea what this does. You can set any mode and the below __get_dimm_mode
        # will return the same value, and it survives soft reboots as well as hard power cycles.
        # It seems to have no effect on the system.
        self.__exchange_dimm_mode(0, mode)

    def __get_dimm_mode(self) -> int:
        return self.__exchange_dimm_mode(0xFF, 0)

    def __set_key_code(self, keydata: bytes) -> None:
        if len(keydata) != 8:
            raise NetDimmException("Key code must by 8 bytes in length")
        self.__send_packet(NetDimmPacket(0x7F, 0x00, keydata))

    def __upload(self, sequence: int, addr: int, data: bytes, last_chunk: bool) -> None:
        # Upload a chunk of data to the DIMM address "addr". The sequence seems to
        # be just a marking for what number packet this is. The last chunk flag is
        # an indicator for whether this is the last packet or not and gets used to
        # set flag bits. If there is no additional data (the length portion is set
        # to 0xA), the packet will be rejected. The net dimm does not seem to parse
        # the sequence number in fw 3.17 but transfergame.exe sends it. The last
        # short does not seem to do anything and does not appear to even be parsed.
        self.__send_packet(NetDimmPacket(0x04, 0x81 if last_chunk else 0x80, struct.pack("<IIH", sequence, addr, 0) + data))

    def __download(self, addr: int, size: int) -> bytes:
        # This appears to have access to not just the dimm bank on the net dimm, but also
        # some system registers and status. System registers are mirrored so the address
        # 0x3ffeffe0 is the same as 0xfffeffe0. Various official utilities use either
        # address for various operations.
        #
        # One notable register is at address 0xfffeffe0 which will return the following information:
        #
        # 0 - CRC over data has not started.
        # 1 - CRC over data is currently in progress (screen will display now checking...).
        # 2 - CRC over data is correct, game should boot or be running.
        # 3 - CRC over data is incorrect, should be waiting for additional data and CRC stamp.
        self.__send_packet(NetDimmPacket(0x05, 0x00, struct.pack("<II", addr, size)))

        # Read the data back. The flags byte will be 0x80 if the requested data size was
        # too big, and 0x81 if all of the data was able to be returned. It looks like at
        # least for 3.17 this limit is 8192. However, the net dimm will continue sending
        # packets until all data has been received.
        data = b""

        while True:
            chunk = self.__recv_packet()

            if chunk.pktid != 0x04:
                # Yes, they have a bug and they used the upload packet type here.
                raise NetDimmException("Unexpected data returned from download packet!")
            if chunk.length <= 10:
                raise NetDimmException("Unexpected data length returned from download packet!")

            # The sequence is set to 1 for the first packet and then incremented for each
            # subsequent packet until the end of data flag is received. It can be safely
            # discarded in practice. I guess there might be some reason to reassemble with
            # the sequences in the correct order, but as of net dimm 3.17 the firware will
            # always send things back in order one packet at a time.
            _chunksequence, chunkaddr, _ = struct.unpack("<IIH", chunk.data[0:10])
            data += chunk.data[10:]

            if chunk.flags & 0x1 != 0:
                # We finished!
                return data

    def __get_crc_information(self) -> int:
        # Read the system register that the net dimm firmware uses to communicate the
        # current CRC status.
        return cast(int, struct.unpack("<I", self.__download(0xfffeffe0, 4))[0])

    def __get_game_size(self) -> int:
        # Read the system register that the net dimm firmware uses to store the game
        # size after a __set_information call is performed.
        return cast(int, struct.unpack("<I", self.__download(0xffff0004, 4))[0])

    def __disable_crc_check(self) -> None:
        self.__upload(1, 0xfffefff0, struct.pack("<IIII", 0xFFFFFFFF, 0xFFFFFFFF, 0, 0), True)

    def __enable_crc_check(self) -> None:
        self.__upload(1, 0xfffefff0, struct.pack("<IIII", 0, 0, 0, 0), True)

    def __get_information(self) -> NetDimmInfo:
        self.__send_packet(NetDimmPacket(0x18, 0x00))

        # Get the info from the DIMM.
        response = self.__recv_packet()
        if response.pktid != 0x18:
            raise NetDimmException("Unexpected data returned from get info packet!")
        if response.length != 12:
            raise NetDimmException("Unexpected data length returned from get info packet!")

        # I don't know what the second integer half represents. It is "0xC"
        # on both NetDimms I've tried this on. There's no use of it in transfergame.
        # At least on firmware 3.17 this is hardcoded to 0xC so it might be the
        # protocol version?
        unknown, version, game_memory, dimm_memory, crc = struct.unpack("<HHHHI", response.data)

        # Extract version and size string.
        version_high = (version >> 8) & 0xFF
        version_low = (version & 0xFF)

        vhigh_hex = hex(version_high)[2:]
        vlow_hex = hex(version_low)[2:]
        while len(vlow_hex) < 2:
            vlow_hex = "0" + vlow_hex
        version_str = f"{vhigh_hex}.{vlow_hex}"
        try:
            firmware_version = NetDimmVersionEnum(version_str)
        except ValueError:
            firmware_version = NetDimmVersionEnum.VERSION_UNKNOWN

        # Now, query if the game CRC is valid.
        crc_info = self.__get_crc_information()
        if crc_info in {0, 1}:
            # CRC is running, unknown.
            crc_status = CRCStatusEnum.STATUS_CHECKING
        elif crc_info == 2:
            # CRC passes, game should be good to run!
            crc_status = CRCStatusEnum.STATUS_VALID
        elif crc_info == 3:
            # CRC failed, need to send a new game!
            crc_status = CRCStatusEnum.STATUS_INVALID
        elif crc_info == 4:
            # DIMM memory is not supported?
            crc_status = CRCStatusEnum.STATUS_BAD_MEMORY
        elif crc_info == 5:
            # CRC is disabled, no checking done.
            crc_status = CRCStatusEnum.STATUS_DISABLED
        else:
            raise NetDimmException("Unexpected CRC status value returned from download packet!")

        # Now, query the size of the game loaded in bytes.
        game_size = self.__get_game_size()
        if game_size == 0 and crc == 0 and crc_status == CRCStatusEnum.STATUS_VALID:
            # We stamped this with an invalid setup and the next transfer was interrupted.
            crc_status = CRCStatusEnum.STATUS_INVALID

        # Now, query the BIOS control word.
        control = self.__host_control_read()

        return NetDimmInfo(
            current_game_crc=crc,
            current_game_size=game_size,
            game_crc_status=crc_status,
            memory_size=dimm_memory,
            firmware_version=firmware_version,
            available_game_memory=game_memory << 20,
            control_address=control,
        )

    def __set_information(self, crc: int, length: int) -> None:
        # Interestingly enough, this can take up to 28 bytes of data, which the DIMM
        # firmware will CRC and store at a particular offset as a 32-byte chunk that
        # includes that CRC. Some official tools send 3 longs (with the last one set
        # to 0) and some only send two longs. So, I'm not sure what the third value
        # here should be or if it is even necessary. The data that gets written here
        # is available in the system registers at 0xffff0000 if you execute a __download
        # request. The 32 bytes at that address will contain the CRC, the length, some
        # other garbage that is possible to send that I don't understand, and finally
        # the crc over the first 28 bytes.
        self.__send_packet(NetDimmPacket(0x19, 0x00, struct.pack("<III", crc & 0xFFFFFFFF, length, 0)))

    def __upload_file(self, data: Union[bytes, FileBytes], key: Optional[bytes], progress_callback: Optional[Callable[[int, int], None]]) -> None:
        # upload a file into DIMM memory, and optionally encrypt for the given key.
        # note that the re-encryption is obsoleted by just setting a zero-key, which
        # is a magic to disable the decryption.
        crc: int = 0
        addr: int = 0
        total: int = len(data)
        des = DES.new(key[::-1], DES.MODE_ECB) if key else None

        def __encrypt(chunk: bytes) -> bytes:
            if des is None:
                return chunk
            return des.encrypt(chunk[::-1])[::-1]

        # Make sure that if this is interrupted, but the CRC was marked as valid
        # at one point, when we resync we don't accidentally think we're running a game.
        # Wipe out the game section including the CRC over the section at 0xffff0028
        # to ensure the net dimm doesn't think anything is valid.
        self.__upload(1, 0xffff0000, b"\0" * 32, False)

        sequence = 2
        while addr < total:
            self.__print("%08x %d%%\r" % (addr, int(float(addr * 100) / float(total))), newline=False)
            if progress_callback:
                progress_callback(addr, total)

            current = data[addr:(addr + 0x8000)]
            curlen = len(current)
            last_packet = addr + curlen == total

            current = __encrypt(current)
            self.__upload(sequence, addr, current, last_packet)
            crc = zlib.crc32(current, crc)
            addr += curlen
            sequence += 1

        if progress_callback:
            progress_callback(addr, total)
        crc = (~crc) & 0xFFFFFFFF
        self.__print("length: %08x" % addr)
        self.__set_information(crc, addr)

    def __close(self) -> None:
        # Request the net dimm to close the connection and stop listening for additional connections.
        # Unclear why you would want to use this since you have to reboot after doing this.
        self.__send_packet(NetDimmPacket(0x09, 0x00))

    def __restart(self) -> None:
        self.__send_packet(NetDimmPacket(0x0A, 0x00))

    def __set_time_limit(self, minutes: int) -> None:
        # According to the 3.17 firmware, this looks to be minutes? The value is checked to be
        # less than 10, and if so multiplied by 60,000. If not, the default value of 60,000 is used.
        self.__send_packet(NetDimmPacket(0x17, 0x00, struct.pack("<I", minutes)))

    # TODO: We are missing any documentation for packet IDs 0x0B, 0x1F, 0xF0 and 0xF1.
    # At least according to triforcetools.py, 0xF0 is "host_read16" so 0xF1 might be "host_write16"
    # much like there is a peek4/poke4. The rest of the packet types documented in triforcetools.py
    # that don't appear here are not in the master switch statement for 3.17 so they might be from
    # a different version of the net dimm firmware. I have not bothered to document the expected
    # sizes or returns for any of these packets. 0x0B appears to only be for triforce/chihiro, as
    # firmware 3.17 explicitly checks against naomi and returns if it is the current target.
