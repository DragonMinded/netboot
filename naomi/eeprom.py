import struct
from typing import Any, Callable, Generic, Optional, TypeVar, Union, cast, overload

from arcadeutils import FileBytes
from naomi.rom import NaomiEEPROMDefaults


class NaomiEEPRomException(Exception):
    pass


BytesLike = TypeVar("BytesLike", bound=Union[bytes, FileBytes])


class ArrayBridge:
    def __init__(self, parent: "NaomiEEPRom[Any]", valid_callback: Callable[[Union[bytes, FileBytes]], bool], name: str, length: int, offset1: int, offset2: int) -> None:
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

    @data.setter
    def data(self, databytes: bytes) -> None:
        self.__setitem__(slice(0, self.__length, 1), databytes)

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

            if key.step not in {None, 1}:
                raise NaomiEEPRomException(f"Cannot get bytes with stride != 1 for {self.name} EEPRom section!")

            # Arbitrarily choose the first section.
            return self.__parent._data[start:stop]

        else:
            raise NotImplementedError("Not implemented!")

    @overload
    def __setitem__(self, key: int, val: int) -> None:
        ...

    @overload
    def __setitem__(self, key: slice, val: bytes) -> None:
        ...

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
                if isinstance(self.__parent._data, bytes):
                    self.__parent._data = self.__parent._data[:realkey] + val + self.__parent._data[realkey + 1:]
                elif isinstance(self.__parent._data, FileBytes):
                    self.__parent._data[realkey:(realkey + 1)] = val
                else:
                    raise Exception("Logic error!")

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
                if isinstance(self.__parent._data, bytes):
                    self.__parent._data = self.__parent._data[:realstart] + val + self.__parent._data[realstop:]
                elif isinstance(self.__parent._data, FileBytes):
                    self.__parent._data[realstart:realstop] = val
                else:
                    raise Exception("Logic error!")

            if len(self.__parent._data) != 128:
                raise Exception("Logic error!")


class NaomiEEPRom(Generic[BytesLike]):

    @staticmethod
    def default(serial: bytes, system_defaults: Optional[NaomiEEPROMDefaults] = None, game_defaults: Optional[bytes] = None) -> "NaomiEEPRom[bytes]":
        if len(serial) != 4:
            raise NaomiEEPRomException("Invalid game serial!")

        # First, set up game defaults section.
        if game_defaults is None:
            game_settings = b'\xFF' * (128 - (18 * 2))
        else:
            header = NaomiEEPRom.crc(game_defaults) + struct.pack("<BB", len(game_defaults), len(game_defaults))
            game_settings = header + header + game_defaults + game_defaults

            padding_len = (128 - (18 * 2) - len(game_settings))
            if padding_len > 0:
                game_settings += b'\xFF' * padding_len

        # Now, set up the system defaults section.
        system_array = [0x10, *[s for s in serial], 0x09, 0x10, 0x00, 0x01, 0x01, 0x01, 0x00, 0x11, 0x11, 0x11, 0x11]
        if system_defaults is not None:
            # Only want to apply the defaults if the default section we were given requests it.
            if system_defaults.apply_settings:
                if system_defaults.force_vertical:
                    # Bottom half of byte.
                    system_array[0] = (system_array[0] & 0xF0) | 0x01
                if system_defaults.force_silent:
                    # Top half of byte.
                    system_array[0] = (system_array[0] & 0x0F) | 0x00
                if system_defaults.chute == "individual":
                    # Bottom half of byte.
                    system_array[6] = (system_array[6] & 0xF0) | 0x01
                if system_defaults.coin_setting > 0 and system_defaults.coin_setting <= 28:
                    # Whole byte, off by one.
                    system_array[7] = system_defaults.coin_setting - 1
                if system_defaults.coin_setting == 28:
                    # Gotta set individual assignments.
                    system_array[8] = system_defaults.credit_rate
                    system_array[9] = system_defaults.coin_1_rate
                    system_array[10] = system_defaults.coin_2_rate
                    system_array[11] = system_defaults.bonus

                # TODO: Skipping out looking up sequence texts for now. Tecchnically we should be setting the upper
                # and lower nibbles of the last 4 bytes to the system default sequence text values 1-8, but I was lazy.

        # Finally, construct the full EEPROM.
        system = bytes(system_array)
        system_crc = NaomiEEPRom.crc(system)
        return NaomiEEPRom(system_crc + system + system_crc + system + game_settings)

    def __init__(self, data: BytesLike) -> None:
        if len(data) != 128:
            raise NaomiEEPRomException("Invalid EEPROM length!")
        if not self.validate(data):
            raise NaomiEEPRomException("Invalid EEPROM CRC!")

        self._data: Union[bytes, FileBytes]
        if isinstance(data, FileBytes):
            self._data = data.clone()
        elif isinstance(data, bytes):
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
    def validate(data: Union[bytes, FileBytes], *, serial: Optional[bytes] = None) -> bool:
        # First, make sure its the right length.
        if len(data) != 128:
            return False

        # Now, before even checking CRCs (slow), check the serial section.
        if serial is not None and data[3:7] != serial:
            return False

        if not NaomiEEPRom.__validate_system(data):
            return False
        return NaomiEEPRom.__validate_game(data, blank_is_valid=True)

    @staticmethod
    def __validate_system(data: Union[bytes, FileBytes]) -> bool:
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
    def __validate_game(data: Union[bytes, FileBytes], *, blank_is_valid: bool = False) -> bool:
        game_size1, game_size2 = struct.unpack("<BB", data[38:40])
        if game_size1 != game_size2:
            # These numbers should always match.
            return False

        game_size3, game_size4 = struct.unpack("<BB", data[42:44])
        if game_size3 != game_size4:
            # These numbers should always match.
            return False

        if game_size1 == 0xFF and game_size3 == 0xFF and blank_is_valid:
            # This is a blank game settings section.
            return True

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
        # First fix the system CRCs.
        sys_section1 = self._data[2:18]
        sys_section2 = self._data[20:36]

        if isinstance(self._data, bytes):
            self._data = NaomiEEPRom.crc(sys_section1) + self._data[2:18] + NaomiEEPRom.crc(sys_section2) + self._data[20:]
        elif isinstance(self._data, FileBytes):
            self._data[0:2] = NaomiEEPRom.crc(sys_section1)
            self._data[18:20] = NaomiEEPRom.crc(sys_section2)
        else:
            raise Exception("Logic error!")

        # Now, fix game CRCs.
        game_size1, game_size2 = struct.unpack("<BB", self._data[38:40])
        if game_size1 != game_size2:
            # These numbers should always match. Skip fixing the CRCs if they don't.
            return
        game_size3, game_size4 = struct.unpack("<BB", self._data[42:44])
        if game_size3 != game_size4:
            # These numbers should always match. Skip fixing the CRCs if they don't.
            return

        if game_size1 != 0xFF:
            game_section1 = self._data[44:(44 + game_size1)]

            if isinstance(self._data, bytes):
                self._data = self._data[:36] + NaomiEEPRom.crc(game_section1) + self._data[38:]
            elif isinstance(self._data, FileBytes):
                self._data[36:38] = NaomiEEPRom.crc(game_section1)
            else:
                raise Exception("Logic error!")

            if game_size3 != 0xFF:
                game_section2 = self._data[(44 + game_size1):(44 + game_size1 + game_size3)]
                if isinstance(self._data, bytes):
                    self._data = self._data[:40] + NaomiEEPRom.crc(game_section2) + self._data[42:]
                elif isinstance(self._data, FileBytes):
                    self._data[40:42] = NaomiEEPRom.crc(game_section2)
                else:
                    raise Exception("Logic error!")
            else:
                if isinstance(self._data, bytes):
                    self._data = self._data[:40] + struct.pack("<BB", 0xFF, 0xFF) + self._data[42:]
                elif isinstance(self._data, FileBytes):
                    self._data[40:42] = struct.pack("<BB", 0xFF, 0xFF)
                else:
                    raise Exception("Logic error!")
        else:
            if isinstance(self._data, bytes):
                self._data = self._data[:36] + struct.pack("<BB", 0xFF, 0xFF) + self._data[38:]
                self._data = self._data[:40] + struct.pack("<BB", 0xFF, 0xFF) + self._data[42:]
            elif isinstance(self._data, FileBytes):
                self._data[40:42] = struct.pack("<BB", 0xFF, 0xFF)
                self._data[36:38] = struct.pack("<BB", 0xFF, 0xFF)
            else:
                raise Exception("Logic error!")

        if len(self._data) != 128:
            raise Exception("Logic error!")

    @property
    def data(self) -> BytesLike:
        self.__fix_crc()
        return cast(BytesLike, self._data)

    @property
    def serial(self) -> bytes:
        # Arbitrarily choose the first enclave.
        return self._data[3:7]

    @property
    def system(self) -> ArrayBridge:
        return ArrayBridge(self, NaomiEEPRom.__validate_system, "system", 16, 2, 20)

    @system.setter
    def system(self, data: bytes) -> None:
        bridge = ArrayBridge(self, NaomiEEPRom.__validate_system, "system", 16, 2, 20)
        bridge.data = data

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
        if newval == 0:
            # Special case for empty game section.
            newval = 0xFF
        lengthbytes = struct.pack("<BB", newval, newval)
        if isinstance(self._data, bytes):
            self._data = self._data[:38] + lengthbytes + self._data[40:42] + lengthbytes + self._data[44:]
        elif isinstance(self._data, FileBytes):
            self._data[38:40] = lengthbytes
            self._data[42:44] = lengthbytes
        else:
            raise Exception("Logic error!")

        if len(self._data) != 128:
            raise Exception("Logic error!")

        # Force the checksum to be updated so that the "valid" attribute on any
        # ArrayBridge becomes true.
        self.__fix_crc()

    @property
    def game(self) -> ArrayBridge:
        length = self.length
        return ArrayBridge(self, NaomiEEPRom.__validate_game, "game", length, 44, 44 + length)

    @game.setter
    def game(self, data: bytes) -> None:
        length = self.length
        bridge = ArrayBridge(self, NaomiEEPRom.__validate_game, "game", length, 44, 44 + length)
        bridge.data = data

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

            if key.step not in {None, 1}:
                raise NaomiEEPRomException("Cannot get bytes with stride != 1 for 128-byte EEPROM!")

            return self._data[start:stop]

        else:
            raise NotImplementedError("Not implemented!")

    @overload
    def __setitem__(self, key: int, val: int) -> None:
        ...

    @overload
    def __setitem__(self, key: slice, val: bytes) -> None:
        ...

    def __setitem__(self, key: Union[int, slice], val: Union[int, bytes]) -> None:
        if isinstance(key, int):
            if key in {0, 1, 18, 19, 36, 37, 40, 41}:
                raise NaomiEEPRomException("Cannot manually set CRC bytes!")
            if key < 0 or key >= 128:
                raise NaomiEEPRomException("Cannot set bytes outside of 128-byte EEPRom!")
            if isinstance(val, int):
                val = bytes([val])

            if isinstance(self._data, bytes):
                self._data = self._data[:key] + val + self._data[key + 1:]
            elif isinstance(self._data, FileBytes):
                self._data[key:(key + 1)] = val
            else:
                raise Exception("Logic error!")

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
            if isinstance(self._data, bytes):
                self._data = self._data[:start] + val + self._data[stop:]
            elif isinstance(self._data, FileBytes):
                self._data[start:stop] = val
            else:
                raise Exception("Logic error!")

            if len(self._data) != 128:
                raise Exception("Logic error!")
