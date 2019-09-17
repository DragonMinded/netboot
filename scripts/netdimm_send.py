#!/usr/bin/env python
# Triforce Netfirm Toolbox, put into the public domain.
# Please attribute properly, but only if you want.
import argparse
import socket
import struct
import sys
import zlib

from Crypto.Cipher import DES
from typing import List, Optional


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
        # connect to the Triforce. Port is tcp/10703.
        # note that this port is only open on
        #       - all Type-3 triforces,
        #       - pre-type3 triforces jumpered to satellite mode.
        # - it *should* work on naomi and chihiro, but due to lack of hardware, i didn't try.
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((ip, 10703))
        self.quiet = quiet
        if target is not None and target not in [self.TARGET_CHIHIRO, self.TARGET_NAOMI, self.TARGET_TRIFORCE]:
            raise NetDimmException(f"Invalid target platform {target}")
        self.target = target or self.TARGET_NAOMI
        if version is not None and version not in [self.NETDIMM_VERSION_1_07, self.NETDIMM_VERSION_2_03, self.NETDIMM_VERSION_2_15, self.NETDIMM_VERSION_3_01]:
            raise NetDimmException(f"Invalid NetDimm version {version}")
        self.version = version or self.NETDIMM_VERSION_3_01

    def send(self, data: bytes, key: Optional[bytes] = None) -> None:
        # display "now loading..."
        self._set_mode(0, 1)

        if key:
            # Send the key that we're going to use to encrypt
            self._set_key_code(key)
        else:
            # disable encryption by setting magic zero-key
            self._set_key_code(b"\x00" * 8)

        # uploads file. Also sets "dimm information" (file length and crc32)
        self._upload_file(data, key)

    def reboot(self) -> None:
        # restart host, this wil boot into game
        self._restart()

        # set time limit to 10h. According to some reports, this does not work.
        self._set_time_limit(10 * 60 * 1000)

        if self.target == self.TARGET_TRIFORCE:
            self._patch_boot_id_check()

    def _print(self, string: str, newline: bool = True) -> None:
        if not self.quiet:
            print(string, file=sys.stderr, end="\n" if newline else "")

    def _read(self, num: int) -> bytes:
        # a function to receive a number of bytes with hard blocking
        res: List[bytes] = []
        left: int = num

        while left > 0:
            ret = self.sock.recv(left)
            left -= len(ret)
            res.append(ret)

        return b"".join(res)

    def _poke4(self, addr: int, data: int) -> None:
        self.sock.send(struct.pack("<IIII", 0x1100000C, addr, 0, data))

    def _set_mode(self, v_and: int, v_or: int) -> bytes:
        self.sock.send(struct.pack("<II", 0x07000004, (v_and << 8) | v_or))
        return self._read(0x8)

    def _set_key_code(self, data: bytes) -> None:
        if len(data) != 8:
            raise NetDimmException("Key code must by 8 bytes in length")
        self.sock.send(struct.pack("<I", 0x7F000008) + data)

    def _upload(self, addr: int, data: bytes, mark: int) -> None:
        self.sock.send(struct.pack("<IIIH", 0x04800000 | (len(data) + 0xA) | (mark << 16), 0, addr, 0) + data)

    def _set_information(self, crc: int, length: int) -> None:
        self.sock.send(struct.pack("<IIII", 0x1900000C, crc & 0xFFFFFFFF, length, 0))

    def _upload_file(self, data: bytes, key: Optional[bytes] = None) -> None:
        # upload a file into DIMM memory, and optionally encrypt for the given key.
        # note that the re-encryption is obsoleted by just setting a zero-key, which
        # is a magic to disable the decryption.
        crc: int = 0
        addr: int = 0
        total: int = len(data)
        des = DES.new(key[::-1], DES.MODE_ECB) if key else None

        def _encrypt(chunk: bytes) -> bytes:
            if des is None:
                return chunk
            return des.encrypt(chunk[::-1])[::-1]

        while True:
            self._print("%08x %d%%\r" % (addr, int(float(addr * 100) / float(total))), newline=False)
            current = data[addr:(addr + 0x8000)]
            if not current:
                break

            current = _encrypt(current)
            self._upload(addr, current, 0)
            crc = zlib.crc32(current, crc)
            addr += len(current)

        self._print("length: %08x" % addr)
        crc = ~crc
        self._upload(addr, b"12345678", 1)
        self._set_information(crc, addr)

    def _restart(self) -> None:
        self.sock.send(struct.pack("<I", 0x0A000000))

    def _set_time_limit(self, limit: int) -> None:
        # TODO: Given the way this is called, I don't know if limit is seconds or milliseconds.
        self.sock.send(struct.pack("<II", 0x17000004, limit))

    def _patch_boot_id_check(self) -> None:
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
            self._poke4(addr + 0, 0x4800001C)
        else:
            self._poke4(addr + 0, 0x4e800020)
            self._poke4(addr + 4, 0x38600000)
            self._poke4(addr + 8, 0x4e800020)
            self._poke4(addr + 0, 0x60000000)  # TODO: This looks suspect, maybe + 12 instead?


def main() -> int:
    parser = argparse.ArgumentParser(description="Tools for sending images to NetDimm for Naomi/Chihiro/Triforce.")
    parser.add_argument(
        "ip",
        metavar="IP",
        type=str,
        help="The IP address that the NetDimm is configured on.",
    )
    parser.add_argument(
        "image",
        metavar="IMAGE",
        type=str,
        help="The image file we should send to the NetDimm.",
    )
    parser.add_argument(
        "--key",
        metavar="HEX",
        type=str,
        help="Key (as a 16 character hex string) to encrypt image file. Defaults to null key",
    )
    parser.add_argument(
        "--target",
        metavar="TARGET",
        type=str,
        help="Target platform this image is going to. Defaults to 'naomi', but 'chihiro' and 'triforce' are also valid",
    )
    parser.add_argument(
        "--version",
        metavar="VERSION",
        type=str,
        help="NetDimm firmware version this image is going to. Defaults to '3.01', but '1.07', '2.03' and '2.15' are also valid",
    )

    args = parser.parse_args()

    # If the user specifies a key (not normally done), convert it
    key: Optional[bytes] = None
    if args.key:
        if len(args.key) != 16:
            raise Exception("Invalid key length for image!")
        key = bytes([int(args.key[x:(x + 2)], 16) for x in range(0, len(args.key), 2)])

    print("connecting...", file=sys.stderr)
    netdimm = NetDimm(args.ip, target=args.target, version=args.version)
    print("ok!", file=sys.stderr)
    with open(args.image, "rb") as fp:
        netdimm.send(fp.read(), key)
    print("rebooting into game...", file=sys.stderr)
    netdimm.reboot()
    print("ok!", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())

# ok, now you're on your own, the tools are there.
# We see the DIMM space as it's seen by the dimm-board (i.e. as on the disc).
# It will be transparently decrypted when accessed from Host, unless a
# zero-key has been set. We do this before uploading something, so we don't
# have to bother with the inserted key chip. Still, some key chip must be
# present.
# You need to configure the triforce to boot in "satellite mode",
# which can be done using the dipswitches on the board (type-3) or jumpers
# (VxWorks-style).
# The dipswitch for type-3 must be in the following position:
#       - SW1: ON ON *
#       - It shouldn't wait for a GDROM anymore, but display error 31.
# For the VxWorks-Style:
#       - Locate JP1..JP3 on the upper board in the DIMM board. They are near
#               the GDROM-connector.
#               The jumpers must be in this position for satellite mode:
#               1               3
#               [. .].  JP1
#               [. .].  JP2
#                .[. .] JP3
#       - when you switch on the triforce, it should say "waiting for network..."
#
# Good Luck. Warez are evil.
