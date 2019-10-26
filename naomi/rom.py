from typing import List


class NaomiRomException(Exception):
    pass


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
