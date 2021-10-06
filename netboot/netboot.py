#!/usr/bin/env python3
# Triforce Netfirm Toolbox, put into the public domain.
# Please attribute properly, but only if you want.
import socket
import struct
import zlib
from Crypto.Cipher import DES
from contextlib import contextmanager
from enum import Enum
from typing import Callable, Generator, List, Optional

from netboot.log import log


class NetDimmException(Exception):
    pass


class TargetEnum(Enum):
    TARGET_CHIHIRO = "chihiro"
    TARGET_NAOMI = "naomi"
    TARGET_TRIFORCE = "triforce"


class TargetVersionEnum(Enum):
    TARGET_VERSION_UNKNOWN = "UNKNOWN"
    TARGET_VERSION_1_07 = "1.07"
    TARGET_VERSION_2_03 = "2.03"
    TARGET_VERSION_2_15 = "2.15"
    TARGET_VERSION_3_01 = "3.01"
    TARGET_VERSION_4_01 = "4.01"
    TARGET_VERSION_4_02 = "4.02"


class NetDimmInfo:
    def __init__(self, current_game_crc: int, memory_size: int, firmware_version: TargetVersionEnum, available_game_memory: int) -> None:
        self.current_game_crc = current_game_crc
        self.memory_size = memory_size
        self.firmware_version = firmware_version
        self.available_game_memory = available_game_memory


class NetDimm:
    def __init__(self, ip: str, target: Optional[TargetEnum] = None, version: Optional[TargetVersionEnum] = None, quiet: bool = False) -> None:
        self.ip: str = ip
        self.sock: Optional[socket.socket] = None
        self.quiet: bool = quiet
        self.target: TargetEnum = target or TargetEnum.TARGET_NAOMI
        self.version: TargetVersionEnum = version or TargetVersionEnum.TARGET_VERSION_UNKNOWN

    def __repr__(self) -> str:
        return f"NetDimm(ip={repr(self.ip)}, target={repr(self.target)}, version={repr(self.version)})"

    def info(self) -> NetDimmInfo:
        with self.__connection():
            # Now, ask for DIMM firmware info and such.
            self.__get_information()

            # Get the info from the DIMM.
            info = self.__read(16)
            ints = struct.unpack("<IHIHI", info)

            if (ints[0] & 0xFF000000) != 0x18000000:
                raise NetDimmException("NetDimm replied with unknown data!")

            # I don't know what the lower 24 bits of the header byte represent.
            # the bottom byte is "0xC" which corresponds to the length of the
            # payload so it might be the length of the data response. This seems
            # to match the rest of the protocol requests and responses.
            if (ints[0] & 0x0000FFFF) != 0xC:
                raise NetDimmException("NetDimm replied with unknown data length!")

            # I don't know what the second integer half represents. It is "0xC"
            # on both NetDimms I've tried this on. There's no use of it in transfergame.

            # Extract version and size string.
            version = 0xFFFF & ints[2]
            version_str = f"{(version >> 8) & 0xFF}.{(version & 0xFF):02}"
            try:
                firmware_version = TargetVersionEnum(version_str)
            except ValueError:
                firmware_version = TargetVersionEnum.TARGET_VERSION_UNKNOWN
            memory = 0xFFFF & (ints[2] >> 16)

            return NetDimmInfo(
                current_game_crc=ints[4],
                memory_size=ints[3],
                firmware_version=firmware_version,
                available_game_memory=memory << 20,
            )

    def send(self, data: bytes, key: Optional[bytes] = None, progress_callback: Optional[Callable[[int, int], None]] = None) -> None:
        with self.__connection():
            # First, signal back to calling code that we've started
            if progress_callback:
                progress_callback(0, len(data))

            # Display "now loading..." on the cabinet screen
            self.__set_mode(0, 1)

            if key:
                # Send the key that we're going to use to encrypt
                self.__set_key_code(key)
            else:
                # disable encryption by setting magic zero-key
                self.__set_key_code(b"\x00" * 8)

            # uploads file. Also sets "dimm information" (file length and crc32)
            self.__upload_file(data, key, progress_callback or (lambda _cur, _tot: None))

    def reboot(self) -> None:
        with self.__connection():
            # restart host, this wil boot into game
            self.__restart()

            # set time limit to 10h. According to some reports, this does not work.
            self.__set_time_limit(10 * 60 * 1000)

            if self.target == TargetEnum.TARGET_TRIFORCE:
                self.__patch_boot_id_check()

    def __print(self, string: str, newline: bool = True) -> None:
        if not self.quiet:
            log(string, newline=newline)

    def __read(self, num: int) -> bytes:
        if self.sock is None:
            raise NetDimmException("Not connected to NetDimm")

        # a function to receive a number of bytes with hard blocking
        res: List[bytes] = []
        left: int = num

        while left > 0:
            ret = self.sock.recv(left)
            left -= len(ret)
            res.append(ret)

        return b"".join(res)

    def __write(self, data: bytes) -> None:
        if self.sock is None:
            raise NetDimmException("Not connected to NetDimm")
        self.sock.send(data)

    @contextmanager
    def __connection(self) -> Generator[None, None, None]:
        # connect to the Triforce. Port is tcp/10703.
        # note that this port is only open on
        #       - all Type-3 triforces,
        #       - pre-type3 triforces jumpered to satellite mode.
        # - it *should* work on naomi and chihiro, but due to lack of hardware, i didn't try.
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(1)
            self.sock.connect((self.ip, 10703))
            self.sock.settimeout(10)
        except Exception as e:
            raise NetDimmException("Could not connect to NetDimm") from e

        try:
            self.__startup()

            yield
        finally:
            self.sock.close()
            self.sock = None

    def __startup(self) -> None:
        self.__write(struct.pack("<I", 0x01000000))

    def __poke4(self, addr: int, data: int) -> None:
        self.__write(struct.pack("<IIII", 0x1100000C, addr, 0, data))

    def __set_mode(self, v_and: int, v_or: int) -> bytes:
        self.__write(struct.pack("<II", 0x07000004, (v_and << 8) | v_or))
        return self.__read(0x8)

    def __set_key_code(self, data: bytes) -> None:
        if len(data) != 8:
            raise NetDimmException("Key code must by 8 bytes in length")
        self.__write(struct.pack("<I", 0x7F000008) + data)

    def __upload(self, addr: int, data: bytes, mark: int) -> None:
        self.__write(struct.pack("<IIIH", 0x04800000 | (len(data) + 0xA) | (mark << 16), 0, addr, 0) + data)

    def __get_information(self) -> None:
        self.__write(struct.pack("<I", 0x18000000))

    def __set_information(self, crc: int, length: int) -> None:
        self.__write(struct.pack("<IIII", 0x1900000C, crc & 0xFFFFFFFF, length, 0))

    def __upload_file(self, data: bytes, key: Optional[bytes], progress_callback: Optional[Callable[[int, int], None]]) -> None:
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

        while True:
            self.__print("%08x %d%%\r" % (addr, int(float(addr * 100) / float(total))), newline=False)
            if progress_callback:
                progress_callback(addr, total)

            current = data[addr:(addr + 0x8000)]
            if not current:
                break

            current = __encrypt(current)
            self.__upload(addr, current, 0)
            crc = zlib.crc32(current, crc)
            addr += len(current)

        if addr != total:
            raise Exception("Logic error!")
        self.__print("length: %08x" % addr)
        crc = ~crc
        self.__upload(addr, b"12345678", 1)
        self.__set_information(crc, addr)

    def __restart(self) -> None:
        self.__write(struct.pack("<I", 0x0A000000))

    def __set_time_limit(self, limit: int) -> None:
        # TODO: Given the way this is called, I don't know if limit is seconds or milliseconds.
        self.__write(struct.pack("<II", 0x17000004, limit))

    def __patch_boot_id_check(self) -> None:
        # this essentially removes a region check, and is triforce-specific; It's also segaboot-version specific.
        # - look for string: "CLogo::CheckBootId: skipped."
        # - binary-search for lower 16bit of address
        addr = {
            TargetVersionEnum.TARGET_VERSION_1_07: 0x8000d8a0,
            TargetVersionEnum.TARGET_VERSION_2_03: 0x8000CC6C,
            TargetVersionEnum.TARGET_VERSION_2_15: 0x8000CC6C,
            TargetVersionEnum.TARGET_VERSION_3_01: 0x8000dc5c,
        }.get(self.version, None)

        if addr is None:
            # We can't do anything here.
            return

        if self.version == TargetVersionEnum.TARGET_VERSION_3_01:
            self.__poke4(addr + 0, 0x4800001C)
        else:
            self.__poke4(addr + 0, 0x4e800020)
            self.__poke4(addr + 4, 0x38600000)
            self.__poke4(addr + 8, 0x4e800020)
            self.__poke4(addr + 0, 0x60000000)  # TODO: This looks suspect, maybe + 12 instead?
