import struct
from typing import Callable, Optional, Union, cast, overload


class NaomiEEPRomException(Exception):
    pass


class ArrayBridge:
    def __init__(self, parent: "NaomiEEPRom", valid_callback: Callable[[bytes], bool], name: str, length: int, offset1: int, offset2: int) -> None:
        self.name = name
        self.__callback = valid_callback
        self.__length = length
        self.__offset1 = offset1
        self.__offset2 = offset2
        self.__parent = parent

    @property
    def valid(self) -> bool:
        return self.__callback(self.__parent._data)

    @property
    def data(self) -> bytes:
        return self.__parent._data[self.__offset1:(self.__offset1 + self.__length)]

    @overload
    def __getitem__(self, key: int) -> int:
        ...

    @overload
    def __getitem__(self, key: slice) -> bytes:
        ...

    def __getitem__(self, key: Union[int, slice]) -> Union[int, bytes]:
        if isinstance(key, int):
            if key < 0 or key >= self.__length:
                raise NaomiEEPRomException(f"Cannot get bytes outside of {self.name} EEPRom section!")

            # Arbitrarily choose the first section.
            realkey = key + self.__offset1
            return self.__parent._data[realkey]

        elif isinstance(key, slice):
            # Determine start of slice
            if key.start is None:
                start = self.__offset1
            elif key.start < 0:
                raise Exception("Do not support negative indexing!")
            else:
                start = self.__offset1 + key.start

            if key.stop is None:
                stop = self.__offset1 + self.__length
            elif key.stop < 0:
                raise Exception("Do not support negative indexing!")
            elif key.stop > self.__length:
                raise NaomiEEPRomException(f"Cannot get bytes outside of {self.name} EEPRom section!")
            else:
                stop = self.__offset1 + key.stop

            # Arbitrarily choose the first section.
            return self.__parent._data[slice(start, stop, key.step)]

        else:
            raise NotImplementedError("Not implemented!")

    def __setitem__(self, key: Union[int, slice], val: Union[int, bytes]) -> None:
        if isinstance(key, int):
            if key < 0 or key >= self.__length:
                raise NaomiEEPRomException(f"Cannot set bytes outside of {self.name} EEPRom section!")
            if isinstance(val, int):
                val = bytes([val])

            if len(val) != 1:
                raise NaomiEEPRomException(f"Cannot change size of {self.name} EEPRom section when assigning!")

            # Make sure we set both bytes in each section.
            for realkey in [key + self.__offset1, key + self.__offset2]:
                self.__parent._data = self.__parent._data[:realkey] + val + self.__parent._data[realkey + 1:]

            # Sanity check what we did here.
            if len(self.__parent._data) != 128:
                raise Exception("Logic error!")

        else:
            # Determine start of slice
            if key.start is None:
                start = 0
            elif key.start < 0:
                raise Exception("Do not support negative indexing!")
            else:
                start = key.start

            if key.stop is None:
                stop = self.__length
            elif key.stop < 0:
                raise Exception("Do not support negative indexing!")
            elif key.stop > self.__length:
                raise NaomiEEPRomException(f"Cannot set bytes outside of {self.name} EEPRom section!")
            else:
                stop = key.stop

            if key.step not in {None, 1}:
                raise NaomiEEPRomException(f"Cannot set bytes with stride != 1 for {self.name} EEPRom section!")

            # Check input to function.
            if not isinstance(val, bytes):
                raise NaomiEEPRomException(f"Cannot change size of {self.name} EEPRom section when assigning!")
            if len(val) != (stop - start):
                raise NaomiEEPRomException(f"Cannot change size of {self.name} EEPRom section when assigning!")

            # Make sure we set both bytes in each section.
            for realstart, realstop in [
                (start + self.__offset1, stop + self.__offset1),
                (start + self.__offset2, stop + self.__offset2),
            ]:
                self.__parent._data = self.__parent._data[:realstart] + val + self.__parent._data[realstop:]

            if len(self.__parent._data) != 128:
                raise Exception("Logic error!")


class NaomiEEPRom:

    @staticmethod
    def default(serial: bytes, game_defaults: Optional[bytes] = None) -> "NaomiEEPRom":
        if len(serial) != 4:
            raise NaomiEEPRomException("Invalid game serial!")

        if game_defaults is None:
            game_settings = b'\xFF' * (128 - (18 * 2))
        else:
            header = NaomiEEPRom.crc(game_defaults) + struct.pack("<BB", len(game_defaults), len(game_defaults))
            game_settings = header + header + game_defaults + game_defaults

            padding_len = (128 - (18 * 2) - len(game_settings))
            if padding_len > 0:
                game_settings += b'\xFF' * padding_len

        system = bytes([0x10]) + serial + bytes([0x09, 0x10, 0x00, 0x01, 0x01, 0x01, 0x00, 0x11, 0x11, 0x11, 0x11])
        system_crc = NaomiEEPRom.crc(system)
        return NaomiEEPRom(system_crc + system + system_crc + system + game_settings)

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
        # First, make sure its the right length.
        if len(data) != 128:
            return False

        if not NaomiEEPRom.__validate_system(data):
            return False
        if only_system:
            return True
        return NaomiEEPRom.__validate_game(data)

    @staticmethod
    def __validate_system(data: bytes) -> bool:
        # Returns whether the settings chunk passes CRC.
        sys_section1 = data[2:18]
        sys_section2 = data[20:36]

        if data[0:2] != NaomiEEPRom.crc(sys_section1):
            # The CRC doesn't match!
            return False
        if data[18:20] != NaomiEEPRom.crc(sys_section2):
            # The CRC doesn't match!
            return False

        # System section is good
        return True

    @staticmethod
    def __validate_game(data: bytes) -> bool:
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
    def serial(self) -> bytes:
        # Arbitrarily choose the first enclave.
        return self._data[3:7]

    @property
    def system(self) -> ArrayBridge:
        return ArrayBridge(self, NaomiEEPRom.__validate_system, "system", 16, 2, 20)

    @property
    def length(self) -> int:
        # Arbitrarily choose the first enclave.
        if not NaomiEEPRom.__validate_game(self._data):
            return 0
        return cast(int, struct.unpack("<B", self._data[38:39])[0])

    @length.setter
    def length(self, newval: int) -> None:
        if newval < 0 or newval > 42:
            raise NaomiEEPRomException("Game section length invalid!")
        lengthbytes = struct.pack("<BB", newval, newval)
        self._data = self._data[:38] + lengthbytes + self._data[40:42] + lengthbytes + self._data[44:]
        if len(self._data) != 128:
            raise Exception("Logic error!")

        # Force the checksum to be updated so that the "valid" attribute on any
        # ArrayBridge becomes true.
        self.__fix_crc()

    @property
    def game(self) -> ArrayBridge:
        length = self.length
        return ArrayBridge(self, NaomiEEPRom.__validate_game, "game", length, 44, 44 + length)

    @overload
    def __getitem__(self, key: int) -> int:
        ...

    @overload
    def __getitem__(self, key: slice) -> bytes:
        ...

    def __getitem__(self, key: Union[int, slice]) -> Union[int, bytes]:
        if isinstance(key, int):
            if key < 0 or key >= 128:
                raise NaomiEEPRomException("Cannot get bytes outside of 128-byte EEPRom!")

            # Arbitrarily choose the first section.
            return self._data[key]

        elif isinstance(key, slice):
            # Determine start of slice
            if key.start is None:
                start = 0
            elif key.start < 0:
                raise Exception("Do not support negative indexing!")
            else:
                start = key.start

            if key.stop is None:
                stop = 128
            elif key.stop < 0:
                raise Exception("Do not support negative indexing!")
            elif key.stop > 128:
                raise NaomiEEPRomException("Cannot get bytes outside of 128-byte EEPRom!")
            else:
                stop = key.stop

            return self._data[slice(start, stop, key.step)]

        else:
            raise NotImplementedError("Not implemented!")

    def __setitem__(self, key: Union[int, slice], val: Union[int, bytes]) -> None:
        if isinstance(key, int):
            if key in {0, 1, 18, 19, 36, 37, 40, 41}:
                raise NaomiEEPRomException("Cannot manually set CRC bytes!")
            if key < 0 or key >= 128:
                raise NaomiEEPRomException("Cannot set bytes outside of 128-byte EEPRom!")
            if isinstance(val, int):
                val = bytes([val])

            self._data = self._data[:key] + val + self._data[key + 1:]
            if len(self._data) != 128:
                raise Exception("Logic error!")

        else:
            # Determine start of slice
            if key.start is None:
                start = 0
            elif key.start < 0:
                raise Exception("Do not support negative indexing!")
            else:
                start = key.start

            if key.stop is None:
                stop = 128
            elif key.stop < 0:
                raise Exception("Do not support negative indexing!")
            elif key.stop > 128:
                raise NaomiEEPRomException("Cannot set bytes outside of 128-byte EEPRom!")
            else:
                stop = key.stop

            if key.step not in {None, 1}:
                raise NaomiEEPRomException("Cannot set bytes with stride != 1 for 128-byte EEPRom!")

            # Check input to function.
            if not isinstance(val, bytes):
                raise NaomiEEPRomException("Cannot change size of 128-byte EEPRom assigning!")
            if len(val) != (stop - start):
                raise NaomiEEPRomException("Cannot change size of 128-byte EEPRom when assigning!")
            for off in range(start, stop):
                if off in {0, 1, 18, 19, 36, 37, 40, 41}:
                    raise NaomiEEPRomException("Cannot manually set CRC bytes!")

            # Now, set the bytes.
            self._data = self._data[:start] + val + self._data[stop:]

            if len(self._data) != 128:
                raise Exception("Logic error!")
