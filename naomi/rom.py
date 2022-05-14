import datetime
import struct
from enum import Enum
from typing import Dict, List, Union, cast

from arcadeutils import FileBytes


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


class NaomiRomRegionEnum(Enum):
    REGION_JAPAN = 0
    REGION_USA = 1
    REGION_EXPORT = 2
    REGION_KOREA = 3
    REGION_AUSTRALIA = 4


class NaomiRomVersionEnum(Enum):
    VERSION_NAOMI_1 = 1
    VERSION_NAOMI_2 = 2


class NaomiEEPROMDefaults:
    def __init__(
        self,
        region: NaomiRomRegionEnum,
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

    HEADER_LENGTH = 0x500

    def __init__(self, data: Union[bytes, FileBytes]) -> None:
        self.data = data[:NaomiRom.HEADER_LENGTH]
        self.data = self.data + (b'\0' * (NaomiRom.HEADER_LENGTH - len(self.data)))

    @staticmethod
    def default() -> "NaomiRom":
        return NaomiRom(b'NAOMI           ' + (b'\0' * (NaomiRom.HEADER_LENGTH - 17)) + b'\xFF')

    @property
    def valid(self) -> bool:
        # TODO: Support encrypted ROM headers. For now, we don't support this so we check
        # the encryption flag and state that the ROM isn't valid if it is set.
        return (
            self.data[0x000:0x010] in {b'NAOMI           ', b'Naomi2          '} and self.data[0x4FF] == 0xFF
        )

    def _raise_on_invalid(self) -> None:
        if not self.valid:
            raise NaomiRomException("Not a valid Naomi ROM!")

    def _inject(self, offset: int, data: bytes) -> None:
        self.data = self.data[:offset] + data + self.data[(offset + len(data)):]
        if len(self.data) != self.HEADER_LENGTH:
            raise Exception("Logic error!")

    def _inject_str(self, offset: int, string: str, pad_length: int) -> None:
        data = string.encode('ascii')
        data = data[:pad_length]
        data = data + (b' ' * (pad_length - len(data)))
        self._inject(offset, data)

    def _inject_uint32(self, offset: int, value: int) -> None:
        self._inject(offset, struct.pack("<I", value))

    def _inject_uint16(self, offset: int, value: int) -> None:
        self._inject(offset, struct.pack("<H", value))

    def _inject_uint8(self, offset: int, value: int) -> None:
        self._inject(offset, struct.pack("<B", value))

    def _sanitize_str(self, data: bytes) -> str:
        return data.decode('ascii').replace("\x00", " ").strip()

    def _sanitize_uint32(self, offset: int) -> int:
        return cast(int, struct.unpack("<I", self.data[offset:(offset + 4)])[0])

    def _sanitize_uint16(self, offset: int) -> int:
        return cast(int, struct.unpack("<H", self.data[offset:(offset + 2)])[0])

    def _sanitize_uint8(self, offset: int) -> int:
        return cast(int, struct.unpack("<B", self.data[offset:(offset + 1)])[0])

    @property
    def version(self) -> NaomiRomVersionEnum:
        self._raise_on_invalid()
        if self.data[0x000:0x010] == b'NAOMI           ':
            return NaomiRomVersionEnum.VERSION_NAOMI_1
        elif self.data[0x000:0x010] == b'Naomi2          ':
            return NaomiRomVersionEnum.VERSION_NAOMI_2
        else:
            raise Exception("Logic error!")

    @version.setter
    def version(self, val: NaomiRomVersionEnum) -> None:
        self._raise_on_invalid()
        if val == NaomiRomVersionEnum.VERSION_NAOMI_1:
            self._inject(0x000, b'NAOMI           ')
        elif val == NaomiRomVersionEnum.VERSION_NAOMI_2:
            self._inject(0x000, b'Naomi2          ')
        else:
            raise NaomiRomException(f"Invalid Naomi header version value {val}!")

    @property
    def publisher(self) -> str:
        self._raise_on_invalid()
        return self._sanitize_str(self.data[0x010:0x030])

    @publisher.setter
    def publisher(self, val: str) -> None:
        self._raise_on_invalid()
        self._inject_str(0x010, val, 0x030 - 0x010)

    @property
    def names(self) -> Dict[NaomiRomRegionEnum, str]:
        self._raise_on_invalid()

        return {
            NaomiRomRegionEnum.REGION_JAPAN: self._sanitize_str(self.data[0x030:0x050]),
            NaomiRomRegionEnum.REGION_USA: self._sanitize_str(self.data[0x050:0x070]),
            NaomiRomRegionEnum.REGION_EXPORT: self._sanitize_str(self.data[0x070:0x090]),
            NaomiRomRegionEnum.REGION_KOREA: self._sanitize_str(self.data[0x090:0x0B0]),
            NaomiRomRegionEnum.REGION_AUSTRALIA: self._sanitize_str(self.data[0x0B0:0x0D0]),
        }

    @names.setter
    def names(self, val: Dict[NaomiRomRegionEnum, str]) -> None:
        self._raise_on_invalid()

        for (region, start, end) in [
            (NaomiRomRegionEnum.REGION_JAPAN, 0x030, 0x050),
            (NaomiRomRegionEnum.REGION_USA, 0x050, 0x070),
            (NaomiRomRegionEnum.REGION_EXPORT, 0x070, 0x090),
            (NaomiRomRegionEnum.REGION_KOREA, 0x090, 0x0B0),
            (NaomiRomRegionEnum.REGION_AUSTRALIA, 0x0B0, 0x0D0),
            # The following are regions that we don't care to write since they
            # are not used in practice.
            (None, 0x0D0, 0x0F0),
            (None, 0x0F0, 0x110),
            (None, 0x110, 0x130),
        ]:
            if region is None:
                self._inject_str(start, "", end - start)
            else:
                self._inject_str(start, val[region], end - start)

    @property
    def sequencetexts(self) -> List[str]:
        self._raise_on_invalid()

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

    @sequencetexts.setter
    def sequencetexts(self, val: List[str]) -> None:
        self._raise_on_invalid()
        if len(val) > 8:
            raise NaomiRomException("Expected list of eight strings for sequence texts!")
        while len(val) < 8:
            val.append('')

        for i, (start, end) in enumerate([
            (0x260, 0x280),
            (0x280, 0x2A0),
            (0x2A0, 0x2C0),
            (0x2C0, 0x2E0),
            (0x2E0, 0x300),
            (0x300, 0x320),
            (0x320, 0x340),
            (0x340, 0x360),
        ]):
            self._inject_str(start, val[i], end - start)

    @property
    def defaults(self) -> Dict[NaomiRomRegionEnum, NaomiEEPROMDefaults]:
        self._raise_on_invalid()

        sections: Dict[NaomiRomRegionEnum, NaomiEEPROMDefaults] = {}
        texts: List[str] = self.sequencetexts

        # It turns out that somebody tagged some of the Atomiswave conversions
        # floating around the internet, so we must do error checking here to
        # avoid their garbage in the header.
        def get_text(val: int) -> str:
            try:
                return texts[val]
            except IndexError:
                return texts[0]

        # The following list must remain sorted in order to output correctly.
        for region in [
            NaomiRomRegionEnum.REGION_JAPAN, NaomiRomRegionEnum.REGION_USA, NaomiRomRegionEnum.REGION_EXPORT, NaomiRomRegionEnum.REGION_KOREA, NaomiRomRegionEnum.REGION_AUSTRALIA
        ]:
            location = 0x1E0 + (0x10 * region.value)
            sequences = [get_text(self._sanitize_uint8(location + 8 + x)) for x in range(8)]

            sections[region] = NaomiEEPROMDefaults(
                region=region,
                apply_settings=self._sanitize_uint8(location + 0) == 1,
                force_vertical=(self._sanitize_uint8(location + 1) & 0x1) != 0,
                force_silent=(self._sanitize_uint8(location + 1) & 0x2) != 0,
                chute="individual" if self._sanitize_uint8(location + 2) != 0 else "common",
                coin_setting=self._sanitize_uint8(location + 3),
                coin_1_rate=self._sanitize_uint8(location + 4),
                coin_2_rate=self._sanitize_uint8(location + 5),
                credit_rate=self._sanitize_uint8(location + 6),
                bonus=self._sanitize_uint8(location + 7),
                sequences=sequences,
            )
        return sections

    @defaults.setter
    def defaults(self, val: Dict[NaomiRomRegionEnum, NaomiEEPROMDefaults]) -> None:
        self._raise_on_invalid()

        # Go through the list, back-converting each structure
        for region, defaults in val.items():
            if region != defaults.region:
                raise Exception(f"Logic error, region key {region} does not match defaults region value {defaults}!")

            offset = 0x1E0 + (0x10 * region.value)

            self._inject_uint8(offset + 0, 1 if defaults.apply_settings else 0)

            mask: int = 0
            if defaults.force_vertical:
                mask |= 0x1
            if defaults.force_silent:
                mask |= 0x2
            self._inject_uint8(offset + 1, mask)

            if defaults.chute == "individual":
                self._inject_uint8(offset + 2, 1)
            elif defaults.chute == "common":
                self._inject_uint8(offset + 2, 0)
            else:
                raise NaomiRomException(f"Invalid chute type {defaults.chute} in defaults!")

            self._inject_uint8(offset + 3, defaults.coin_setting)
            self._inject_uint8(offset + 4, defaults.coin_1_rate)
            self._inject_uint8(offset + 5, defaults.coin_2_rate)
            self._inject_uint8(offset + 6, defaults.credit_rate)
            self._inject_uint8(offset + 7, defaults.bonus)

            # Calculate sequence offsets
            sequences: List[str] = list(defaults.sequences)
            if len(sequences) > 8:
                raise NaomiRomException("Invalid number of sequence texts for defaults, expected at most 8!")

            # First, default them all to 0
            self._inject(offset + 8, b'\0' * 8)

            # Now, attempt to insert texts that are non-default
            for i, text in enumerate(sequences):
                # First, if it is empty, default it
                if text == "":
                    continue

                # Try to find this sequence text in our global list.
                # This is not cached because it can change.
                validtexts = self.sequencetexts
                for off, existingtext in enumerate(validtexts):
                    if text == existingtext:
                        self._inject_uint8(offset + 8 + i, off)
                        break
                else:
                    # We didn't find one, add it to the end!
                    for off, existingtext in enumerate(validtexts):
                        if existingtext == "":
                            validtexts[off] = text
                            self.sequencetexts = validtexts
                            self._inject_uint8(offset + 8 + i, off)
                            break
                    else:
                        # We didn't find room!
                        raise NaomiRomException(f"Not enough room in sequence text table for {text}!")

    @property
    def date(self) -> datetime.date:
        self._raise_on_invalid()
        year = self._sanitize_uint16(0x130)
        month = self._sanitize_uint8(0x132)
        day = self._sanitize_uint8(0x133)
        if year == 0:
            year = 2000
        if month == 0:
            month = 1
        if day == 0:
            day = 1
        return datetime.date(year, month, day)

    @date.setter
    def date(self, date: datetime.date) -> None:
        self._raise_on_invalid()
        self._inject_uint16(0x130, date.year)
        self._inject_uint8(0x132, date.month)
        self._inject_uint8(0x133, date.day)

    @property
    def serial(self) -> bytes:
        self._raise_on_invalid()
        return self.data[0x134:0x138]

    @serial.setter
    def serial(self, serial: bytes) -> None:
        self._raise_on_invalid()
        if len(serial) != 4:
            raise NaomiRomException("Serial length should be 4 bytes!")
        if serial[0:1] != b'B':
            raise NaomiRomException("Serial should start with B!")
        self._inject(0x134, serial)

    @property
    def regions(self) -> List[NaomiRomRegionEnum]:
        self._raise_on_invalid()
        mask = self._sanitize_uint8(0x428)
        regions: List[NaomiRomRegionEnum] = []
        for region in [NaomiRomRegionEnum.REGION_JAPAN, NaomiRomRegionEnum.REGION_USA, NaomiRomRegionEnum.REGION_EXPORT, NaomiRomRegionEnum.REGION_KOREA, NaomiRomRegionEnum.REGION_AUSTRALIA]:
            if ((mask >> region.value) & 0x1) != 0:
                regions.append(region)
        return regions

    @regions.setter
    def regions(self, regions: List[NaomiRomRegionEnum]) -> None:
        self._raise_on_invalid()

        mask: int = 0
        for region in regions:
            mask |= 1 << region.value
        self._inject_uint8(0x428, mask)

    @property
    def players(self) -> List[int]:
        self._raise_on_invalid()
        mask = self._sanitize_uint8(0x429)
        if mask == 0:
            return [1, 2, 3, 4]
        return sorted([
            (x + 1) for x in range(4)
            if ((mask >> x) & 0x1) != 0
        ])

    @players.setter
    def players(self, players: List[int]) -> None:
        self._raise_on_invalid()

        mask: int = 0
        for player in players:
            if player not in {1, 2, 3, 4}:
                raise NaomiRomException(f"Number of players {player} not valid!")
            mask |= 1 << (player - 1)
        self._inject_uint8(0x429, mask)

    @property
    def frequencies(self) -> List[int]:
        self._raise_on_invalid()
        mask = self._sanitize_uint8(0x42A)
        if mask == 0:
            return [15, 31]
        lut = [31, 15]
        return sorted([
            lut[x] for x in range(2)
            if ((mask >> x) & 0x1) != 0
        ])

    @frequencies.setter
    def frequencies(self, frequencies: List[int]) -> None:
        self._raise_on_invalid()

        mask: int = 0
        for frequency in frequencies:
            if frequency not in {15, 31}:
                raise NaomiRomException(f"Monitor frequency {frequency} not valid!")
            if frequency == 15:
                mask |= 0x2
            if frequency == 31:
                mask |= 0x1
        self._inject_uint8(0x42A, mask)

    @property
    def orientations(self) -> List[str]:
        self._raise_on_invalid()
        mask = self._sanitize_uint8(0x42B)
        lut = ['horizontal', 'vertical']
        if mask == 0:
            return lut
        return sorted([
            lut[x] for x in range(2)
            if ((mask >> x) & 0x1) != 0
        ])

    @orientations.setter
    def orientations(self, orientations: List[str]) -> None:
        self._raise_on_invalid()

        mask: int = 0
        for orientation in orientations:
            if orientation not in {'horizontal', 'vertical'}:
                raise NaomiRomException(f"Monitor orientation {orientation} not valid!")
            if orientation == "horizontal":
                mask |= 0x1
            if orientation == "vertical":
                mask |= 0x2
        self._inject_uint8(0x42B, mask)

    @property
    def servicetype(self) -> str:
        self._raise_on_invalid()
        return 'individual' if self._sanitize_uint8(0x42D) != 0 else 'common'

    @servicetype.setter
    def servicetype(self, servicetype: str) -> None:
        self._raise_on_invalid()
        if servicetype == "individual":
            self._inject_uint8(0x42D, 1)
        elif servicetype == "common":
            self._inject_uint8(0x42D, 0)
        else:
            raise NaomiRomException(f"Service type {servicetype} not valid!")

    def _get_sections(self, startoffset: int) -> List[NaomiRomSection]:
        sections: List[NaomiRomSection] = []
        for entry in range(8):
            location = startoffset + 12 * entry

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
        return sections

    def _put_sections(self, offset: int, sections: List[NaomiRomSection]) -> None:
        if len(sections) > 8:
            raise NaomiRomException("Cannot have more than 8 load sections in an executable!")

        for section in sections:
            self._inject_uint32(offset, section.offset)
            self._inject_uint32(offset + 4, section.load_address)
            self._inject_uint32(offset + 8, section.length)
            offset += 12

        if len(sections) < 8:
            # Put an end of list marker so we don't load any garbage data.
            self._inject_uint32(offset, 0xFFFFFFFF)

    @property
    def main_executable(self) -> NaomiExecutable:
        self._raise_on_invalid()
        return NaomiExecutable(
            sections=self._get_sections(0x360),
            entrypoint=self._sanitize_uint32(0x420),
        )

    @main_executable.setter
    def main_executable(self, val: NaomiExecutable) -> None:
        self._raise_on_invalid()
        self._put_sections(0x360, val.sections)
        self._inject_uint32(0x420, val.entrypoint)

    @property
    def test_executable(self) -> NaomiExecutable:
        self._raise_on_invalid()
        return NaomiExecutable(
            sections=self._get_sections(0x3C0),
            entrypoint=self._sanitize_uint32(0x424),
        )

    @test_executable.setter
    def test_executable(self, val: NaomiExecutable) -> None:
        self._raise_on_invalid()
        self._put_sections(0x3C0, val.sections)
        self._inject_uint32(0x424, val.entrypoint)
