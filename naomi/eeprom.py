import struct
from typing import cast


class NaomiEEPRomException(Exception):
    pass


class ArrayBridge:
    def __init__(self, parent: "NaomiEEPRom", name: str, length: int, offset1: int, offset2: int) -> None:
        self.name = name
        self.__length = length
        self.__offset1 = offset1
        self.__offset2 = offset2
        self.__parent = parent

    @property
    def data(self) -> bytes:
        return self.__parent._data[self.__offset1:(self.__offset1 + self.__length)]

    def __getitem__(self, key: int) -> bytes:
        if key < 0 or key >= self.__length:
            raise NaomiEEPRomException(f"Cannot get bytes outside of {self.name} EEPRom section!")

        # Arbitrarily choose the first section.
        realkey = key + self.__offset1
        return self.__parent._data[realkey:(realkey + 1)]

    def __setitem__(self, key: int, val: bytes) -> None:
        if len(val) != 1:
            raise NaomiEEPRomException("Cannot set more than one byte at a time!")
        if key < 0 or key >= self.__length:
            raise NaomiEEPRomException(f"Cannot set bytes outside of {self.name} EEPRom section!")

        # Make sure we set both bytes in each section.
        for realkey in [key + self.__offset1, key + self.__offset2]:
            self.__parent._data = self.__parent._data[:realkey] + val + self.__parent._data[realkey + 1:]
        if len(self.__parent._data) != 128:
            raise Exception("Logic error!")


class NaomiEEPRom:

    @staticmethod
    def default(serial: bytes) -> "NaomiEEPRom":
        if len(serial) != 4:
            raise NaomiEEPRomException("Invalid game serial!")
        data = bytes([0x10]) + serial + bytes([0x18, 0x10, 0x00, 0x01, 0x01, 0x01, 0x00, 0x11, 0x11, 0x11, 0x11])
        return NaomiEEPRom(NaomiEEPRom.crc(data) + data + NaomiEEPRom.crc(data) + data + b'\0xFF' * (128 - (18 * 2)))

    def __init__(self, data: bytes) -> None:
        if not self.validate(data, only_system=True):
            raise NaomiEEPRomException("Invalid EEPROM CRC!")
        self._data = data

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

    @staticmethod
    def validate(data: bytes, *, only_system: bool = False) -> bool:
        # Returns whether the settings chunk passes CRC.
        sys_section1 = data[2:18]
        sys_section2 = data[20:36]

        game_size1, game_size2 = struct.unpack("<BB", data[38:40])
        if game_size1 != game_size2:
            # These numbers should always match.
            return False

        game_size3, game_size4 = struct.unpack("<BB", data[42:44])
        if game_size3 != game_size4:
            # These numbers should always match.
            return False

        game_section1 = data[44:(44 + game_size1)]
        game_section2 = data[(44 + game_size1):(44 + game_size1 + game_size3)]

        if data[0:2] != NaomiEEPRom.crc(sys_section1):
            # The CRC doesn't match!
            return False
        if data[18:20] != NaomiEEPRom.crc(sys_section2):
            # The CRC doesn't match!
            return False

        if not only_system:
            if data[36:38] != NaomiEEPRom.crc(game_section1):
                # The CRC doesn't match!
                return False
            if data[40:42] != NaomiEEPRom.crc(game_section2):
                # The CRC doesn't match!
                return False

        # Everything looks good!
        return True

    def __fix_crc(self) -> None:
        # Grab a local reference.
        data = self._data

        # First fix the system CRCs.
        sys_section1 = data[2:18]
        sys_section2 = data[20:36]
        data = NaomiEEPRom.crc(sys_section1) + data[2:18] + NaomiEEPRom.crc(sys_section2) + data[20:]

        # Now, fix game CRCs.
        game_size1, game_size2 = struct.unpack("<BB", data[38:40])
        if game_size1 != game_size2:
            # These numbers should always match. Skip fixing the CRCs if they don't.
            return
        game_size3, game_size4 = struct.unpack("<BB", data[42:44])
        if game_size3 != game_size4:
            # These numbers should always match. Skip fixing the CRCs if they don't.
            return

        if game_size1 != 0xFF and game_size1 != 0x0:
            game_section1 = data[44:(44 + game_size1)]
            data = data[:36] + NaomiEEPRom.crc(game_section1) + data[38:]

            if game_size3 != 0xFF and game_size3 != 0x0:
                game_section2 = data[(44 + game_size1):(44 + game_size1 + game_size3)]
                data = data[:40] + NaomiEEPRom.crc(game_section2) + data[42:]

        if len(data) != 128:
            raise Exception("Logic error!")
        self._data = data

    @property
    def data(self) -> bytes:
        self.__fix_crc()
        return self._data

    @property
    def system(self) -> ArrayBridge:
        return ArrayBridge(self, "system", 16, 2, 20)

    @property
    def length(self) -> int:
        return cast(int, struct.unpack("<B", self._data[38:39])[0])

    @length.setter
    def length(self, newval: int) -> None:
        if newval < 0 or newval > 42:
            raise NaomiEEPRomException("Game section length invalid!")
        lengthbytes = struct.pack("<BB", (newval, newval))
        self._data = self._data[:38] + lengthbytes + self._data[40:42] + lengthbytes + self._data[44:]
        if len(self._data) != 128:
            raise Exception("Logic error!")

    @property
    def game(self) -> ArrayBridge:
        length = self.length
        return ArrayBridge(self, "game", length, 44, 44 + length)

    def __getitem__(self, key: int) -> bytes:
        if key < 0 or key >= 128:
            raise NaomiEEPRomException("Cannot get bytes outside of 128-byte EEPRom!")
        return self._data[key:(key + 1)]

    def __setitem__(self, key: int, val: bytes) -> None:
        if len(val) != 1:
            raise NaomiEEPRomException("Cannot set more than one byte at a time!")
        if key in {0, 1, 18, 19, 36, 37, 40, 41}:
            raise NaomiEEPRomException("Cannot manually set CRC bytes!")
        if key < 0 or key >= 128:
            raise NaomiEEPRomException("Cannot set bytes outside of 128-byte EEPRom!")
        self._data = self._data[:key] + val + self._data[key + 1:]
        if len(self._data) != 128:
            raise Exception("Logic error!")
