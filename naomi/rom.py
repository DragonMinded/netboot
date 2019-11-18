import datetime
import struct
from typing import List, cast


class NaomiRomException(Exception):
    pass


class NaomiRomSection:
    def __init__(self, offset: int, length: int, load_address: int) -> None:
        self.offset = offset
        self.length = length
        self.load_address = load_address

    def __repr__(self) -> str:
        return f"NaomiRomSection(offset={self.offset}, length={self.length}, load_address={hex(self.load_address)})"


class NaomiExecutable:
    def __init__(self, entrypoint: int, sections: List[NaomiRomSection]) -> None:
        self.sections = sections
        self.entrypoint = entrypoint

    def __repr__(self) -> str:
        return f"NaomiExecutable(entrypoint={hex(self.entrypoint)}, sections={repr(self.sections)})"


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
        return self.data[0x000:0x010] == b'NAOMI           '

    def _sanitize_str(self, data: bytes) -> str:
        return data.decode('ascii').strip()

    def _sanitize_uint32(self, data: bytes) -> int:
        return cast(int, struct.unpack("<I", data)[0])

    def _sanitize_uint16(self, data: bytes) -> int:
        return cast(int, struct.unpack("<H", data)[0])

    def _sanitize_uint8(self, data: bytes) -> int:
        return cast(int, struct.unpack("<B", data)[0])

    @property
    def publisher(self) -> str:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        return self._sanitize_str(self.data[0x010:0x030])

    @property
    def names(self) -> List[str]:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

        return [
            self._sanitize_str(self.data[0x030:0x050]),
            self._sanitize_str(self.data[0x050:0x070]),
            self._sanitize_str(self.data[0x070:0x090]),
            self._sanitize_str(self.data[0x090:0x0B0]),
            self._sanitize_str(self.data[0x0B0:0x0D0]),
        ]

    @property
    def date(self) -> datetime.date:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        return datetime.date(
            self._sanitize_uint16(self.data[0x130:0x132]),
            self._sanitize_uint8(self.data[0x132:0x133]),
            self._sanitize_uint8(self.data[0x133:0x134]),
        )

    @property
    def serial(self) -> bytes:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        return self.data[0x134:0x138]

    @property
    def main_executable(self) -> NaomiExecutable:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

        sections: List[NaomiRomSection] = []
        for entry in range(8):
            location = 0x360 + 12 * entry

            offset = self._sanitize_uint32(self.data[location:(location + 4)])
            if offset == 0xFFFFFFFF:
                break
            sections.append(
                NaomiRomSection(
                    offset=offset,
                    length=self._sanitize_uint32(self.data[(location + 8):(location + 12)]),
                    load_address=self._sanitize_uint32(self.data[(location + 4):(location + 8)]),
                )
            )

        return NaomiExecutable(
            sections=sections,
            entrypoint=self._sanitize_uint32(self.data[0x420:0x424]),
        )

    @property
    def test_executable(self) -> NaomiExecutable:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

        sections: List[NaomiRomSection] = []
        for entry in range(8):
            location = 0x3C0 + 12 * entry

            offset = self._sanitize_uint32(self.data[location:(location + 4)])
            if offset == 0xFFFFFFFF:
                break
            sections.append(
                NaomiRomSection(
                    offset=offset,
                    length=self._sanitize_uint32(self.data[(location + 8):(location + 12)]),
                    load_address=self._sanitize_uint32(self.data[(location + 4):(location + 8)]),
                )
            )

        return NaomiExecutable(
            sections=sections,
            entrypoint=self._sanitize_uint32(self.data[0x424:0x428]),
        )
