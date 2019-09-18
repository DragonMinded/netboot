#!/usr/bin/env python3
# Triforce Netfirm Toolbox, put into the public domain.
# Please attribute properly, but only if you want.
import socket
import struct
import sys
import zlib

from Crypto.Cipher import DES
from contextlib import contextmanager
from typing import Callable, Generator, List, Optional


class NetDimmException(Exception):
    pass


class NetDimm:
    TARGET_CHIHIRO = "chihiro"
    TARGET_NAOMI = "naomi"
    TARGET_TRIFORCE = "triforce"

    NETDIMM_VERSION_1_07 = "1.07"
    NETDIMM_VERSION_2_03 = "2.03"
    NETDIMM_VERSION_2_15 = "2.15"
    NETDIMM_VERSION_3_01 = "3.01"

    def __init__(self, ip: str, target: Optional[str] = None, version: Optional[str] = None, quiet: bool = False) -> None:
        self.ip: str = ip
        self.sock: Optional[socket.socket] = None
        self.quiet: bool = quiet
        if target is not None and target not in [self.TARGET_CHIHIRO, self.TARGET_NAOMI, self.TARGET_TRIFORCE]:
            raise NetDimmException(f"Invalid target platform {target}")
        self.target: str = target or self.TARGET_NAOMI
        if version is not None and version not in [self.NETDIMM_VERSION_1_07, self.NETDIMM_VERSION_2_03, self.NETDIMM_VERSION_2_15, self.NETDIMM_VERSION_3_01]:
            raise NetDimmException(f"Invalid NetDimm version {version}")
        self.version: str = version or self.NETDIMM_VERSION_3_01

    def send(self, data: bytes, key: Optional[bytes] = None, progress_callback: Optional[Callable[[int, int], None]] = None) -> None:
        with self.__connection():
            # display "now loading..."
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

            if self.target == self.TARGET_TRIFORCE:
                self.__patch_boot_id_check()

    def __print(self, string: str, newline: bool = True) -> None:
        if not self.quiet:
            print(string, file=sys.stderr, end="\n" if newline else "")

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
            self.sock.connect((self.ip, 10703))
        except Exception as e:
            raise NetDimmException("Could not connect to NetDimm") from e

        try:
            yield
        finally:
            self.sock.close()
            self.sock = None

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
            self.NETDIMM_VERSION_1_07: 0x8000d8a0,
            self.NETDIMM_VERSION_2_03: 0x8000CC6C,
            self.NETDIMM_VERSION_2_15: 0x8000CC6C,
            self.NETDIMM_VERSION_3_01: 0x8000dc5c,
        }[self.version]

        if self.version == self.NETDIMM_VERSION_3_01:
            self.__poke4(addr + 0, 0x4800001C)
        else:
            self.__poke4(addr + 0, 0x4e800020)
            self.__poke4(addr + 4, 0x38600000)
            self.__poke4(addr + 8, 0x4e800020)
            self.__poke4(addr + 0, 0x60000000)  # TODO: This looks suspect, maybe + 12 instead?
