import struct


class NaomiEEPRomException(Exception):
    pass


class NaomiEEPRom:

    @staticmethod
    def default(serial: bytes) -> "NaomiEEPRom":
        if len(serial) != 4:
            raise NaomiEEPRomException("Invalid game serial!")
        data = bytes([0x10]) + serial + bytes([0x18, 0x10, 0x00, 0x01, 0x01, 0x01, 0x00, 0x11, 0x11, 0x11, 0x11])
        return NaomiEEPRom(NaomiEEPRom.crc(data) + data)

    def __init__(self, data: bytes) -> None:
        if self.crc(data[2:]) != data[0:2]:
            raise NaomiEEPRomException("Invalid EEPROM CRC!")
        self.__data = data[2:]

    @staticmethod
    def __cap_32(val: int) -> int:
        return val & 0xFFFFFFFF

    @staticmethod
    def __crc_inner(running_crc: int, next_byte: int) -> int:
        # First, mask off the values so we don't get a collision
        running_crc &= 0xFFFFFF00
        next_byte &= 0xFF

        # Add the byte into the CRC
        running_crc = NaomiEEPRom.__cap_32(running_crc + next_byte)

        for _ in range(8):
            if running_crc < 0x80000000:
                running_crc = NaomiEEPRom.__cap_32(running_crc * 2)
            else:
                running_crc = NaomiEEPRom.__cap_32(running_crc * 2)
                running_crc = NaomiEEPRom.__cap_32(running_crc + 0x10210000)

        return running_crc

    @staticmethod
    def crc(data: bytes) -> bytes:
        running_crc = 0xDEBDEB00

        for byte in b"".join([data, b"\x00"]):
            running_crc = NaomiEEPRom.__crc_inner(running_crc, byte)

        final_crc = (running_crc >> 16) & 0xFFFF
        return struct.pack("<H", final_crc)

    @property
    def data(self) -> bytes:
        data = self.__data
        crc = self.crc(data)
        return crc + data

    def __getitem__(self, key: int) -> bytes:
        return self.__data[key:(key + 1)]

    def __setitem__(self, key: int, val: bytes) -> None:
        if len(val) != 1:
            raise NaomiEEPRomException("Cannot set more than one byte at a time!")
        self.__data = self.__data[:key] + val + self.__data[key + 1:]
