import struct
from typing import List, cast


class NaomiRomException(Exception):
    pass


class NaomiRomSection:
    def __init__(self, offset: int, length: int, load_address: int, entrypoint: int) -> None:
        self.offset = offset
        self.length = length
        self.load_address = load_address
        self.entrypoint = entrypoint

    def __repr__(self) -> str:
        return f"NaomiRomSection(offset={self.offset}, length={self.length}, load_address={hex(self.load_address)}, entrypoint={hex(self.entrypoint)})"


class NaomiRom:

    REGION_JAPAN = 0
    REGION_USA = 1
    REGION_EXPORT = 2
    REGION_KOREA = 3
    REGION_AUSTRALIA = 4

    def __init__(self, data: bytes) -> None:
        self.data = data

    @property
    def valid(self) -> bool:
        return self.data[0x0:0x10] == b'NAOMI           '

    def _sanitize_str(self, data: bytes) -> str:
        return data.decode('ascii').strip()

    def _sanitize_uint32(self, data: bytes) -> int:
        return cast(int, struct.unpack("<I", data)[0])

    @property
    def publisher(self) -> str:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        return self._sanitize_str(self.data[0x10:0x30])

    @property
    def names(self) -> List[str]:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

        return [
            self._sanitize_str(self.data[0x30:0x50]),
            self._sanitize_str(self.data[0x50:0x70]),
            self._sanitize_str(self.data[0x70:0x90]),
            self._sanitize_str(self.data[0x90:0xB0]),
            self._sanitize_str(self.data[0xB0:0xD0]),
        ]

    @property
    def serial(self) -> bytes:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        return self.data[0x134:0x138]

    @property
    def main_executable(self) -> NaomiRomSection:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

        offset = self._sanitize_uint32(self.data[0x360:0x364])
        return NaomiRomSection(
            offset=offset,
            length=self._sanitize_uint32(self.data[0x368:0x36C]) - offset,
            load_address=self._sanitize_uint32(self.data[0x364:0x368]),
            entrypoint=self._sanitize_uint32(self.data[0x420:0x424]),
        )

    @property
    def test_executable(self) -> NaomiRomSection:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

        offset = self._sanitize_uint32(self.data[0x3C0:0x3C4])
        return NaomiRomSection(
            offset=offset,
            length=self._sanitize_uint32(self.data[0x3C8:0x3CC]) - offset,
            load_address=self._sanitize_uint32(self.data[0x3C4:0x3C8]),
            entrypoint=self._sanitize_uint32(self.data[0x424:0x428]),
        )
