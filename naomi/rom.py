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


class NaomiEEPROMDefaults:
    def __init__(
        self,
        region: int,
        apply_settings: bool,
        force_vertical: bool,
        force_silent: bool,
        chute: str,
        coin_setting: int,
        coin_1_rate: int,
        coin_2_rate: int,
        credit_rate: int,
        bonus: int,
        sequences: List[str],
    ):
        self.region = region
        self.apply_settings = apply_settings
        self.force_vertical = force_vertical
        self.force_silent = force_silent
        self.chute = chute
        self.coin_setting = coin_setting
        self.coin_1_rate = coin_1_rate
        self.coin_2_rate = coin_2_rate
        self.credit_rate = credit_rate
        self.bonus = bonus
        self.sequences = sequences

    def __repr__(self) -> str:
        return (
            "NaomiEEPROMDefaults("
            f"region={repr(self.region)}, "
            f"apply_settings={repr(self.apply_settings)}, "
            f"force_vertical={repr(self.force_vertical)}, "
            f"force_silent={repr(self.force_silent)}, "
            f"chute={repr(self.chute)}, "
            f"coin_setting={repr(self.coin_setting)}, "
            f"coin_1_rate={repr(self.coin_1_rate)}, "
            f"coin_2_rate={repr(self.coin_2_rate)}, "
            f"credit_rate={repr(self.credit_rate)}, "
            f"bonus={repr(self.bonus)}, "
            f"sequences={repr(self.sequences)}"
            ")"
        )


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
        return data.decode('ascii').replace("\x00", " ").strip()

    def _sanitize_uint32(self, offset: int) -> int:
        return cast(int, struct.unpack("<I", self.data[offset:(offset + 4)])[0])

    def _sanitize_uint16(self, offset: int) -> int:
        return cast(int, struct.unpack("<H", self.data[offset:(offset + 2)])[0])

    def _sanitize_uint8(self, offset: int) -> int:
        return cast(int, struct.unpack("<B", self.data[offset:(offset + 1)])[0])

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
    def sequencetexts(self) -> List[str]:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

        return [
            self._sanitize_str(self.data[0x260:0x280]),
            self._sanitize_str(self.data[0x280:0x2A0]),
            self._sanitize_str(self.data[0x2A0:0x2C0]),
            self._sanitize_str(self.data[0x2C0:0x2E0]),
            self._sanitize_str(self.data[0x2E0:0x300]),
            self._sanitize_str(self.data[0x300:0x320]),
            self._sanitize_str(self.data[0x320:0x340]),
            self._sanitize_str(self.data[0x340:0x360]),
        ]

    @property
    def defaults(self) -> List[NaomiEEPROMDefaults]:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

        defaults: List[NaomiEEPROMDefaults] = []
        texts: List[str] = self.sequencetexts

        # The following list must remain sorted in order to output correctly.
        for offset in [self.REGION_JAPAN, self.REGION_USA, self.REGION_EXPORT, self.REGION_KOREA, self.REGION_AUSTRALIA]:
            location = 0x1E0 + (0x10 * offset)

            defaults.append(NaomiEEPROMDefaults(
                region=offset,
                apply_settings=self._sanitize_uint8(location) == 1,
                force_vertical=(self._sanitize_uint8(location + 1) & 0x1) != 0,
                force_silent=(self._sanitize_uint8(location + 1) & 0x2) != 0,
                chute="individual" if self._sanitize_uint8(location + 2) != 0 else "common",
                coin_setting=self._sanitize_uint8(location + 3),
                coin_1_rate=self._sanitize_uint8(location + 4),
                coin_2_rate=self._sanitize_uint8(location + 5),
                credit_rate=self._sanitize_uint8(location + 6),
                bonus=self._sanitize_uint8(location + 7),
                sequences=[texts[self._sanitize_uint8(location + 8 + x)] for x in range(8)],
            ))
        return defaults

    @property
    def date(self) -> datetime.date:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        return datetime.date(
            self._sanitize_uint16(0x130),
            self._sanitize_uint8(0x132),
            self._sanitize_uint8(0x133),
        )

    @property
    def serial(self) -> bytes:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        return self.data[0x134:0x138]

    @property
    def regions(self) -> List[int]:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        mask = self._sanitize_uint8(0x428)
        regions: List[int] = []
        for offset in [self.REGION_JAPAN, self.REGION_USA, self.REGION_EXPORT, self.REGION_KOREA, self.REGION_AUSTRALIA]:
            if ((mask >> offset) & 0x1) != 0:
                regions.append(offset)
        return regions

    @property
    def players(self) -> List[int]:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        mask = self._sanitize_uint8(0x429)
        if mask == 0:
            return [1, 2, 3, 4]
        return sorted([
            (x + 1) for x in range(4)
            if ((mask >> x) & 0x1) != 0
        ])

    @property
    def frequencies(self) -> List[int]:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        mask = self._sanitize_uint8(0x42A)
        if mask == 0:
            return [15, 31]
        lut = [31, 15]
        return sorted([
            lut[x] for x in range(2)
            if ((mask >> x) & 0x1) != 0
        ])

    @property
    def orientations(self) -> List[str]:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        mask = self._sanitize_uint8(0x42B)
        lut = ['horizontal', 'vertical']
        if mask == 0:
            return lut
        return sorted([
            lut[x] for x in range(2)
            if ((mask >> x) & 0x1) != 0
        ])

    @property
    def servicetype(self) -> str:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")
        return 'individual' if self._sanitize_uint8(0x42D) != 0 else 'common'

    @property
    def main_executable(self) -> NaomiExecutable:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

        sections: List[NaomiRomSection] = []
        for entry in range(8):
            location = 0x360 + 12 * entry

            offset = self._sanitize_uint32(location)
            if offset == 0xFFFFFFFF:
                break
            sections.append(
                NaomiRomSection(
                    offset=offset,
                    length=self._sanitize_uint32(location + 8),
                    load_address=self._sanitize_uint32(location + 4),
                )
            )

        return NaomiExecutable(
            sections=sections,
            entrypoint=self._sanitize_uint32(0x420),
        )

    @property
    def test_executable(self) -> NaomiExecutable:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

        sections: List[NaomiRomSection] = []
        for entry in range(8):
            location = 0x3C0 + 12 * entry

            offset = self._sanitize_uint32(location)
            if offset == 0xFFFFFFFF:
                break
            sections.append(
                NaomiRomSection(
                    offset=offset,
                    length=self._sanitize_uint32(location + 8),
                    load_address=self._sanitize_uint32(location + 4),
                )
            )

        return NaomiExecutable(
            sections=sections,
            entrypoint=self._sanitize_uint32(0x424),
        )
