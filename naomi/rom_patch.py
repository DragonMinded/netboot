#! /usr/bin/env python3
import os
import struct
from typing import Generic, Optional, Tuple, TypeVar, Union, cast, overload

from arcadeutils import FileBytes
from naomi import NaomiRom, NaomiRomSection, NaomiEEPRom


def get_default_trojan() -> bytes:
    # Specifically for projects including this code as a 3rd-party dependency,
    # look up where we stick the default already-compiled trojan and return it
    # as bytes that can be passed into the "trojan" param of the
    # NaomiSettingsPatcher constructor. This way, they don't need to know our
    # internal directory layout.
    root = os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), ".."))
    trojan = os.path.join(root, 'homebrew', 'settingstrojan', 'settingstrojan.bin')
    with open(trojan, "rb") as bfp:
        return bfp.read()


@overload
def add_or_update_section(
    data: bytes,
    location: int,
    newsection: bytes,
    header: Optional[NaomiRom] = None,
    verbose: bool = False,
) -> bytes:
    ...


@overload
def add_or_update_section(
    data: FileBytes,
    location: int,
    newsection: bytes,
    header: Optional[NaomiRom] = None,
    verbose: bool = False,
) -> FileBytes:
    ...


def add_or_update_section(
    data: Union[bytes, FileBytes],
    location: int,
    newsection: bytes,
    header: Optional[NaomiRom] = None,
    verbose: bool = False,
) -> Union[bytes, FileBytes]:
    # Note that if an external header is supplied, it will be modified to match the updated
    # data.
    if isinstance(data, FileBytes):
        data = data.clone()
    if header is None:
        header = NaomiRom(data)

    # First, find out if there's already a section in this header.
    executable = header.main_executable
    for section in executable.sections:
        if section.load_address == location:
            # This is a section chunk
            if section.length != len(newsection):
                raise NaomiSettingsPatcherException("Found section in executable, but it is the wrong size!")

            if verbose:
                print("Overwriting old section in existing ROM section.")

            # We can just update the data to overwrite this section
            if isinstance(data, bytes):
                data = data[:section.offset] + newsection + data[(section.offset + section.length):]
            elif isinstance(data, FileBytes):
                data[section.offset:(section.offset + section.length)] = newsection
            else:
                raise Exception("Logic error!")
            break
    else:
        # We need to add an init section to the ROM
        if len(executable.sections) >= 8:
            raise NaomiSettingsPatcherException("ROM already has the maximum number of sections!")

        # Add a new section to the end of the rom for this binary data.
        if verbose:
            print("Attaching section to a new ROM section at the end of the file.")

        # Add a new section to the end of the rom for this section
        executable.sections.append(
            NaomiRomSection(
                offset=len(data),
                load_address=location,
                length=len(newsection)
            )
        )
        header.main_executable = executable

        # Now, just append it to the end of the file
        if isinstance(data, bytes):
            data = header.data + data[header.HEADER_LENGTH:] + newsection
        elif isinstance(data, FileBytes):
            data[:header.HEADER_LENGTH] = header.data
            data.append(newsection)
        else:
            raise Exception("Logic error!")

    # Return the updated data.
    return data


def __is_config(data: Union[bytes, FileBytes], index: int) -> bool:
    if all(x == 0xEE for x in data[index:(index + 4)]) and all(x == 0xEE for x in data[(index + 24):(index + 28)]):
        _, _, _, debug, _ = struct.unpack("<IIIII", data[(index + 4):(index + 24)])
        if debug not in {0, 1, 0xDDDDDDDD}:
            return False
        return True
    return False


def __extract_config(data: Union[bytes, FileBytes], index: int) -> Tuple[int, int, bool, Tuple[int, int, int]]:
    original_start, trojan_start, _, debug, date = struct.unpack("<IIIII", data[(index + 4):(index + 24)])

    day = date % 100
    month = (date // 100) % 100
    year = (date // 10000)

    return (
        original_start,
        trojan_start,
        debug != 0,
        (year, month, day),
    )


def get_config(data: Union[bytes, FileBytes], *, start: Optional[int] = None, end: Optional[int] = None) -> Tuple[int, int, bool, Tuple[int, int, int]]:
    # Returns a tuple consisting of the original EXE start address and
    # the desired trojan start address and whether debug printing is
    # enabled, and the date string of the trojan we're using.
    if isinstance(data, bytes):
        for i in range(start or 0, (end or len(data)) - (28 - 1)):
            if __is_config(data, i):
                return __extract_config(data, i)

    elif isinstance(data, FileBytes):
        start = start or 0
        end = end or len(data)

        while start < end:
            location = data.search(b"\xEE" * 4, start=start, end=end)
            if location is None:
                break

            if __is_config(data, location):
                return __extract_config(data, location)
            start = location + 1

    else:
        raise Exception("Logic error!")

    raise NaomiSettingsPatcherException("Couldn't find config in executable!")


@overload
def change(binfile: bytes, tochange: bytes, loc: int) -> bytes:
    ...


@overload
def change(binfile: FileBytes, tochange: bytes, loc: int) -> FileBytes:
    ...


def change(binfile: Union[bytes, FileBytes], tochange: bytes, loc: int) -> Union[bytes, FileBytes]:
    if isinstance(binfile, bytes):
        return binfile[:loc] + tochange + binfile[(loc + len(tochange)):]
    elif isinstance(binfile, FileBytes):
        binfile[loc:(loc + len(tochange))] = tochange
        return binfile
    else:
        raise Exception("Logic error!")


@overload
def patch_bytesequence(data: bytes, sentinel: int, replacement: bytes) -> bytes:
    ...


@overload
def patch_bytesequence(data: FileBytes, sentinel: int, replacement: bytes) -> FileBytes:
    ...


def patch_bytesequence(data: Union[bytes, FileBytes], sentinel: int, replacement: bytes) -> Union[bytes, FileBytes]:
    if isinstance(data, bytes):
        length = len(replacement)
        for i in range(len(data) - (length - 1)):
            if all(x == sentinel for x in data[i:(i + length)]):
                return change(data, replacement, i)

        raise NaomiSettingsPatcherException("Couldn't find spot to patch in data!")

    elif isinstance(data, FileBytes):
        replace_loc = data.search(bytes(sentinel) * len(replacement))
        if replace_loc is not None:
            return change(data, replacement, replace_loc)

        raise NaomiSettingsPatcherException("Couldn't find spot to patch in data!")

    else:
        raise Exception("Logic error!")


@overload
def add_or_update_trojan(
    data: bytes,
    trojan: bytes,
    debug: int,
    options: int,
    datachunk: Optional[bytes] = None,
    header: Optional[NaomiRom] = None,
    verbose: bool = False,
) -> bytes:
    ...


@overload
def add_or_update_trojan(
    data: FileBytes,
    trojan: bytes,
    debug: int,
    options: int,
    datachunk: Optional[bytes] = None,
    header: Optional[NaomiRom] = None,
    verbose: bool = False,
) -> FileBytes:
    ...


def add_or_update_trojan(
    data: Union[bytes, FileBytes],
    trojan: bytes,
    debug: int,
    options: int,
    datachunk: Optional[bytes] = None,
    header: Optional[NaomiRom] = None,
    verbose: bool = False,
) -> Union[bytes, FileBytes]:
    # Note that if an external header is supplied, it will be modified to match the updated
    # data.
    if isinstance(data, FileBytes):
        data = data.clone()
    if header is None:
        header = NaomiRom(data)

    # Grab a safe-to-mutate cop of the trojan, get its current config.
    executable = header.main_executable
    exe = trojan[:]
    _, location, _, _ = get_config(exe)

    for sec in executable.sections:
        if sec.load_address == location:
            # Grab the old entrypoint from the existing modification since the ROM header
            # entrypoint will be the old trojan EXE.
            entrypoint, _, _, _ = get_config(data, start=sec.offset, end=sec.offset + sec.length)
            exe = patch_bytesequence(exe, 0xAA, struct.pack("<I", entrypoint))
            if datachunk:
                exe = patch_bytesequence(exe, 0xBB, datachunk)
            exe = patch_bytesequence(exe, 0xCF, struct.pack("<I", options))
            exe = patch_bytesequence(exe, 0xDD, struct.pack("<I", debug))

            # We can reuse this section, but first we need to get rid of the old patch.
            if sec.offset + sec.length == len(data):
                # We can just stick the new file right on top of where the old was.
                if verbose:
                    print("Overwriting old section in existing ROM section.")

                # Cut off the old section, add our new section, make sure the length is correct.
                if isinstance(data, bytes):
                    data = data[:sec.offset] + exe
                elif isinstance(data, FileBytes):
                    data.truncate(sec.offset)
                    data.append(exe)
                else:
                    raise Exception("Logic error!")
                sec.length = len(exe)
            else:
                # It is somewhere in the middle of an executable, zero it out and
                # then add this section to the end of the ROM.
                if verbose:
                    print("Zeroing out old section in existing ROM section and attaching new section to the end of the file.")

                # Patch the executable with the correct settings and entrypoint.
                data = change(data, b"\0" * sec.length, sec.offset)

                # Repoint the section at the new section
                sec.offset = len(data)
                sec.length = len(exe)
                sec.load_address = location

                # Add the section to the end of the ROM.
                data = data + exe
            break
    else:
        if len(executable.sections) >= 8:
            raise NaomiSettingsPatcherException("ROM already has the maximum number of init sections!")

        # Add a new section to the end of the rom for this binary data.
        if verbose:
            print("Attaching section to a new ROM section at the end of the file.")

        executable.sections.append(
            NaomiRomSection(
                offset=len(data),
                load_address=location,
                length=len(exe),
            )
        )

        # Patch the executable with the correct settings and entrypoint.
        exe = patch_bytesequence(exe, 0xAA, struct.pack("<I", executable.entrypoint))
        if datachunk:
            exe = patch_bytesequence(exe, 0xBB, datachunk)
        exe = patch_bytesequence(exe, 0xCF, struct.pack("<I", options))
        exe = patch_bytesequence(exe, 0xDD, struct.pack("<I", debug))
        data = data + exe

    # Generate new header and attach executable to end of data.
    executable.entrypoint = location
    header.main_executable = executable

    # Now, return the updated ROM with the new header and appended data.
    if isinstance(data, bytes):
        return header.data + data[header.HEADER_LENGTH:]
    elif isinstance(data, FileBytes):
        data[:header.HEADER_LENGTH] = header.data
        return data
    else:
        raise Exception("Logic error!")


class NaomiSettingsPatcherException(Exception):
    pass


class NaomiSettingsDate:
    def __init__(self, year: int, month: int, day: int) -> None:
        self.year = year
        self.month = month
        self.day = day


class NaomiSettingsInfo:
    def __init__(self, debug: bool, date: Tuple[int, int, int]) -> None:
        self.enable_debugging = debug
        self.date = NaomiSettingsDate(date[0], date[1], date[2])


BytesLike = TypeVar("BytesLike", bound=Union[bytes, FileBytes])


class NaomiSettingsPatcher(Generic[BytesLike]):
    SRAM_LOCATION = 0x200000
    SRAM_SIZE = 32768

    EEPROM_SIZE = 128

    MAX_TROJAN_SIZE = 512 * 1024

    def __init__(self, rom: BytesLike, trojan: Optional[bytes]) -> None:
        self.__data: Union[bytes, FileBytes]
        if isinstance(rom, FileBytes):
            self.__data = rom.clone()
        elif isinstance(rom, bytes):
            self.__data = rom
        else:
            raise Exception("Logic error!")
        self.__trojan: Optional[bytes] = trojan
        if trojan is not None and len(trojan) > NaomiSettingsPatcher.MAX_TROJAN_SIZE:
            raise Exception("Logic error! Adjust the max trojan size to match the compiled tojan!")
        self.__rom: Optional[NaomiRom] = None
        self.__has_sram: Optional[bool] = None
        self.__has_eeprom: Optional[bool] = None

    @property
    def data(self) -> BytesLike:
        return cast(BytesLike, self.__data)

    @property
    def serial(self) -> bytes:
        # Parse the ROM header so we can grab the game serial code.
        naomi = self.__rom or NaomiRom(self.__data)
        self.__rom = naomi

        return naomi.serial

    @property
    def rom(self) -> NaomiRom:
        # Grab the entire rom as a parsed structure.
        naomi = self.__rom or NaomiRom(self.__data)
        self.__rom = naomi

        return naomi

    @property
    def has_eeprom(self) -> bool:
        if self.__has_eeprom is None:
            self.__has_eeprom = (self.eeprom_info is not None)
        return self.__has_eeprom

    @property
    def eeprom_info(self) -> Optional[NaomiSettingsInfo]:
        # Parse the ROM header so we can narrow our search.
        naomi = self.__rom or NaomiRom(self.__data)
        self.__rom = naomi

        # Only look at main executables.
        executable = naomi.main_executable
        for sec in executable.sections:
            # Constrain the search to the section that we jump to, since that will always
            # be where our trojan is.
            if executable.entrypoint >= sec.load_address and executable.entrypoint < (sec.load_address + sec.length):
                if sec.length > NaomiSettingsPatcher.MAX_TROJAN_SIZE:
                    # This can't possibly be the trojan, just skip it for speed.
                    continue
                try:
                    # Grab the old entrypoint from the existing modification since the ROM header
                    # entrypoint will be the old trojan EXE.
                    _, _, debug, date = get_config(self.__data, start=sec.offset, end=sec.offset + sec.length)

                    return NaomiSettingsInfo(debug, date)
                except Exception:
                    continue

        return None

    def get_eeprom(self) -> Optional[bytes]:
        # Parse the ROM header so we can narrow our search.
        naomi = self.__rom or NaomiRom(self.__data)
        self.__rom = naomi

        # Only look at main executables.
        executable = naomi.main_executable
        for sec in executable.sections:
            # Constrain the search to the section that we jump to, since that will always
            # be where our trojan is.
            if executable.entrypoint >= sec.load_address and executable.entrypoint < (sec.load_address + sec.length):
                if sec.length > NaomiSettingsPatcher.MAX_TROJAN_SIZE:
                    # This can't possibly be the trojan, just skip it for speed.
                    continue
                try:
                    # Grab the old entrypoint from the existing modification since the ROM header
                    # entrypoint will be the old trojan EXE.
                    get_config(self.__data, start=sec.offset, end=sec.offset + sec.length)

                    # Returns the requested EEPRom that should be written prior to the game starting.
                    for i in range(sec.offset, sec.offset + sec.length - (self.EEPROM_SIZE - 1)):
                        if NaomiEEPRom.validate(self.__data[i:(i + self.EEPROM_SIZE)], serial=naomi.serial):
                            self.__has_eeprom = True
                            return self.__data[i:(i + self.EEPROM_SIZE)]
                except Exception:
                    pass

        # Couldn't find a section that matched.
        self.__has_eeprom = False
        return None

    def put_eeprom(self, eeprom: bytes, *, enable_debugging: bool = False, verbose: bool = False) -> None:
        # First, parse the ROM we were given.
        naomi = self.__rom or NaomiRom(self.__data)

        # Now make sure the EEPROM is valid.
        if len(eeprom) == self.EEPROM_SIZE:
            # First, we need to modify the settings trojan with this ROM's load address and
            # the EEPROM we want to add. Make sure the EEPRom we were given is valid.
            if not NaomiEEPRom.validate(eeprom, serial=naomi.serial):
                raise NaomiSettingsPatcherException("EEPROM is incorrectly formed!")
            if naomi.serial != eeprom[3:7] or naomi.serial != eeprom[21:25]:
                raise NaomiSettingsPatcherException("EEPROM is not for this game!")

        else:
            raise NaomiSettingsPatcherException("Invalid EEPROM size to attach to a Naomi ROM!")

        # Now we need to add an EXE init section to the ROM.
        if self.__trojan is None or not self.__trojan:
            raise NaomiSettingsPatcherException("Cannot have an empty trojan when attaching EEPROM settings!")

        # Patch the trojan onto the ROM, updating the settings in the trojan accordingly.
        self.__data = add_or_update_trojan(
            self.__data,
            self.__trojan,
            1 if enable_debugging else 0,
            0,
            datachunk=eeprom,
            header=naomi,
            verbose=verbose,
        )

        # Also, write back the new ROM.
        self.__rom = naomi
        self.__has_eeprom = True

    @property
    def has_sram(self) -> bool:
        if self.__has_sram is None:
            # Need to see if there is an attached SRAM section in this exe.
            naomi = self.__rom or NaomiRom(self.__data)
            self.__rom = naomi

            for section in naomi.main_executable.sections:
                if section.load_address == self.SRAM_LOCATION and section.length == self.SRAM_SIZE:
                    self.__has_sram = True
                    break
            else:
                self.__has_sram = False
        return self.__has_sram

    def get_sram(self) -> Optional[bytes]:
        # Parse the ROM header so we can narrow our search.
        naomi = self.__rom or NaomiRom(self.__data)
        self.__rom = naomi

        # Only look at main executables.
        executable = naomi.main_executable
        for sec in executable.sections:
            # Check and see if it is a SRAM chunk.
            if sec.load_address == self.SRAM_LOCATION and sec.length == self.SRAM_SIZE:
                # It is!
                self.__has_sram = True
                return self.__data[sec.offset:(sec.offset + sec.length)]

        # Couldn't find a section that matched.
        self.__has_sram = False
        return None

    def put_sram(self, sram: bytes, *, verbose: bool = False) -> None:
        # First, parse the ROM we were given.
        naomi = self.__rom or NaomiRom(self.__data)

        # Now make sure the SRAM is valid.
        if len(sram) != self.SRAM_SIZE:
            raise NaomiSettingsPatcherException("Invalid SRAM size to attach to a Naomi ROM!")

        # Patch the section directly onto the ROM.
        self.__has_sram = True
        self.__data = add_or_update_section(self.__data, self.SRAM_LOCATION, sram, header=naomi, verbose=verbose)

        # Also, write back the new ROM.
        self.__rom = naomi
