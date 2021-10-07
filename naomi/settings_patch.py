#! /usr/bin/env python3
import os
import struct
from enum import Enum
from typing import Optional, Tuple

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


class NaomiSettingsPatcherException(Exception):
    pass


class NaomiSettingsDate:
    def __init__(self, year: int, month: int, day: int) -> None:
        self.year = year
        self.month = month
        self.day = day


class NaomiSettingsInfo:
    def __init__(self, sentinel: bool, debug: bool, date: Tuple[int, int, int]) -> None:
        self.enable_sentinel = sentinel
        self.enable_debugging = debug
        self.date = NaomiSettingsDate(date[0], date[1], date[2])


class NaomiSettingsTypeEnum(Enum):
    TYPE_NONE = 0
    TYPE_EEPROM = 1
    TYPE_SRAM = 2


class NaomiSettingsPatcher:
    SRAM_LOCATION = 0x200000
    SRAM_SIZE = 32768

    EEPROM_SIZE = 128

    MAX_TROJAN_SIZE = 512 * 1024

    def __init__(self, rom: bytes, trojan: Optional[bytes]) -> None:
        self.data: bytes = rom
        self.__trojan: Optional[bytes] = trojan
        if trojan is not None and len(trojan) > NaomiSettingsPatcher.MAX_TROJAN_SIZE:
            raise Exception("Logic error! Adjust the max trojan size to match the compiled tojan!")
        self.__rom: Optional[NaomiRom] = None
        self.__type: Optional[NaomiSettingsTypeEnum] = None

    @staticmethod
    def __change(binfile: bytes, tochange: bytes, loc: int) -> bytes:
        return binfile[:loc] + tochange + binfile[(loc + len(tochange)):]

    @staticmethod
    def __patch_bytesequence(data: bytes, sentinel: int, replacement: bytes) -> bytes:
        length = len(replacement)
        for i in range(len(data) - length + 1):
            if all(x == sentinel for x in data[i:(i + length)]):
                return NaomiSettingsPatcher.__change(data, replacement, i)

        raise NaomiSettingsPatcherException("Couldn't find spot to patch in data!")

    @staticmethod
    def __get_config(data: bytes) -> Tuple[int, int, bool, bool, Tuple[int, int, int]]:
        # Returns a tuple consisting of the original EXE start address and
        # the desired trojan start address, whether sentinel mode is enabled
        # and whether debug printing is enabled, and the date string of the
        # trojan we're using.
        for i in range(len(data) - 28):
            if all(x == 0xEE for x in data[i:(i + 4)]) and all(x == 0xEE for x in data[(i + 24):(i + 28)]):
                original_start, trojan_start, sentinel, debug, date = struct.unpack("<IIIII", data[(i + 4):(i + 24)])
                if sentinel not in {0, 1, 0xCFCFCFCF} or debug not in {0, 1, 0xDDDDDDDD}:
                    continue

                day = date % 100
                month = (date // 100) % 100
                year = (date // 10000)

                return (
                    original_start,
                    trojan_start,
                    sentinel != 0,
                    debug != 0,
                    (year, month, day),
                )

        raise NaomiSettingsPatcherException("Couldn't find config in executable!")

    @property
    def serial(self) -> bytes:
        # Parse the ROM header so we can grab the game serial code.
        naomi = self.__rom or NaomiRom(self.data)
        self.__rom = naomi

        return naomi.serial

    @property
    def rom(self) -> NaomiRom:
        # Grab the entire rom as a parsed structure.
        naomi = self.__rom or NaomiRom(self.data)
        self.__rom = naomi

        return naomi

    @property
    def type(self) -> NaomiSettingsTypeEnum:
        if self.__type is None:
            # First, try looking at the start address of any executable sections.
            naomi = self.__rom or NaomiRom(self.data)
            self.__rom = naomi

            for section in naomi.main_executable.sections:
                if section.load_address == self.SRAM_LOCATION and section.length == self.SRAM_SIZE:
                    self.__type = NaomiSettingsTypeEnum.TYPE_SRAM
                    break
            else:
                if self.info is not None:
                    # This is for sure an EEPROM settings, we only get info about the
                    # settings for EEPROM type since it is an executable with configuration.
                    self.__type = NaomiSettingsTypeEnum.TYPE_EEPROM
                else:
                    # We have no settings attached.
                    self.__type = NaomiSettingsTypeEnum.TYPE_NONE

        # Now, return the calculated type.
        if self.__type is None:
            raise Exception("Logic error!")
        return self.__type

    @property
    def info(self) -> Optional[NaomiSettingsInfo]:
        # Parse the ROM header so we can narrow our search.
        naomi = self.__rom or NaomiRom(self.data)
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
                    data = self.data[sec.offset:(sec.offset + sec.length)]
                    _, _, sentinel, debug, date = self.__get_config(data)

                    return NaomiSettingsInfo(sentinel, debug, date)
                except Exception:
                    continue

        return None

    def get_settings(self) -> Optional[bytes]:
        # Parse the ROM header so we can narrow our search.
        naomi = self.__rom or NaomiRom(self.data)
        self.__rom = naomi

        # Only look at main executables.
        executable = naomi.main_executable
        for sec in executable.sections:
            # First, check and see if it is a SRAM settings.
            if sec.load_address == self.SRAM_LOCATION and sec.length == self.SRAM_SIZE:
                # It is!
                if self.__type is None:
                    self.__type = NaomiSettingsTypeEnum.TYPE_SRAM
                elif self.__type != NaomiSettingsTypeEnum.TYPE_SRAM:
                    raise Exception("Logic error!")
                return self.data[sec.offset:(sec.offset + sec.length)]

            # Constrain the search to the section that we jump to, since that will always
            # be where our trojan is.
            if executable.entrypoint >= sec.load_address and executable.entrypoint < (sec.load_address + sec.length):
                if sec.length > NaomiSettingsPatcher.MAX_TROJAN_SIZE:
                    # This can't possibly be the trojan, just skip it for speed.
                    continue
                try:
                    # Grab the old entrypoint from the existing modification since the ROM header
                    # entrypoint will be the old trojan EXE.
                    data = self.data[sec.offset:(sec.offset + sec.length)]
                    self.__get_config(data)

                    # Returns the requested EEPRom settings that should be written prior
                    # to the game starting.
                    for i in range(len(data) - self.EEPROM_SIZE):
                        if NaomiEEPRom.validate(data[i:(i + self.EEPROM_SIZE)]):
                            if self.__type is None:
                                self.__type = NaomiSettingsTypeEnum.TYPE_EEPROM
                            elif self.__type != NaomiSettingsTypeEnum.TYPE_EEPROM:
                                raise Exception("Logic error!")
                            return data[i:(i + self.EEPROM_SIZE)]
                except Exception:
                    pass

        # Couldn't find a section that matched.
        if self.__type is None:
            self.__type = NaomiSettingsTypeEnum.TYPE_NONE
        elif self.__type != NaomiSettingsTypeEnum.TYPE_NONE:
            raise Exception("Logic error!")
        return None

    def put_settings(self, settings: bytes, *, enable_sentinel: bool = False, enable_debugging: bool = False, verbose: bool = False) -> None:
        # First, parse the ROM we were given.
        naomi = self.__rom or NaomiRom(self.data)

        # Now, determine the type of the settings.
        if len(settings) == self.EEPROM_SIZE:
            # First, we need to modify the settings trojan with this ROM's load address and
            # the EEPROM we want to add. Make sure the EEPRom we were given is valid.
            if not NaomiEEPRom.validate(settings):
                raise NaomiSettingsPatcherException("Settings is incorrectly formed!")
            if naomi.serial != settings[3:7] or naomi.serial != settings[21:25]:
                raise NaomiSettingsPatcherException("Settings is not for this game!")
            if self.__type == NaomiSettingsTypeEnum.TYPE_SRAM:
                # This is technically feasible but we don't have the interface to make it work
                # and I am not sure making this code more complicated is worth it.
                raise NaomiSettingsPatcherException("Cannot attach both an EEPROM and an SRAM settings file!")
            self.__type = NaomiSettingsTypeEnum.TYPE_EEPROM

        elif len(settings) == self.SRAM_SIZE:
            if self.__type == NaomiSettingsTypeEnum.TYPE_EEPROM:
                # This is technically feasible but we don't have the interface to make it work
                # and I am not sure making this code more complicated is worth it.
                raise NaomiSettingsPatcherException("Cannot attach both an EEPROM and an SRAM settings file!")
            self.__type = NaomiSettingsTypeEnum.TYPE_SRAM

        else:
            raise NaomiSettingsPatcherException("Unknown settings type to attach to Naomi ROM!")

        # Finally, make the requested modification.
        if self.__type == NaomiSettingsTypeEnum.TYPE_EEPROM:
            # Now we need to add an EXE init section to the ROM.
            if self.__trojan is None or not self.__trojan:
                raise NaomiSettingsPatcherException("Cannot have an empty trojan when attaching EEPROM settings!")
            exe = self.__trojan[:]
            executable = naomi.main_executable
            _, location, _, _, _ = self.__get_config(exe)

            for sec in executable.sections:
                if sec.load_address == location:
                    # Grab the old entrypoint from the existing modification since the ROM header
                    # entrypoint will be the old trojan EXE.
                    entrypoint, _, _, _, _ = self.__get_config(self.data[sec.offset:(sec.offset + sec.length)])
                    exe = self.__patch_bytesequence(exe, 0xAA, struct.pack("<I", entrypoint))
                    exe = self.__patch_bytesequence(exe, 0xBB, settings)
                    exe = self.__patch_bytesequence(exe, 0xCF, struct.pack("<I", 1 if enable_sentinel else 0))
                    exe = self.__patch_bytesequence(exe, 0xDD, struct.pack("<I", 1 if enable_debugging else 0))

                    # We can reuse this section, but first we need to get rid of the old patch.
                    if sec.offset + sec.length == len(self.data):
                        # We can just stick the new file right on top of where the old was.
                        if verbose:
                            print("Overwriting old settings in existing ROM section.")

                        # Cut off the old section, add our new section, make sure the length is correct.
                        self.data = self.data[:sec.offset] + exe
                        sec.length = len(exe)
                    else:
                        # It is somewhere in the middle of an executable, zero it out and
                        # then add this section to the end of the ROM.
                        if verbose:
                            print("Zeroing out old settings in existing ROM section and attaching new settings to the end of the file.")

                        # Patch the executable with the correct settings and entrypoint.
                        self.data = self.__change(self.data, b"\0" * sec.length, sec.offset)
                        sec.offset = len(self.data)
                        sec.length = len(exe)
                        sec.load_address = location
                        self.data += exe
                    break
            else:
                if len(executable.sections) >= 8:
                    raise NaomiSettingsPatcherException("ROM already has the maximum number of init sections!")

                # Add a new section to the end of the rom for this binary data.
                if verbose:
                    print("Attaching settings to a new ROM section at the end of the file.")

                executable.sections.append(
                    NaomiRomSection(
                        offset=len(self.data),
                        load_address=location,
                        length=len(exe),
                    )
                )

                # Patch the executable with the correct settings and entrypoint.
                exe = self.__patch_bytesequence(exe, 0xAA, struct.pack("<I", executable.entrypoint))
                exe = self.__patch_bytesequence(exe, 0xBB, settings)
                exe = self.__patch_bytesequence(exe, 0xCF, struct.pack("<I", 1 if enable_sentinel else 0))
                exe = self.__patch_bytesequence(exe, 0xDD, struct.pack("<I", 1 if enable_debugging else 0))
                self.data += exe

            executable.entrypoint = location

            # Generate new header and attach executable to end of data.
            naomi.main_executable = executable

            # Now, save this back so it can be read by other callers.
            self.data = naomi.data + self.data[naomi.HEADER_LENGTH:]

        elif self.__type == NaomiSettingsTypeEnum.TYPE_SRAM:
            # First, find out if there's already an SRAM portion to the file
            executable = naomi.main_executable
            for section in executable.sections:
                if section.load_address == self.SRAM_LOCATION:
                    # This is a SRAM load chunk
                    if section.length != self.SRAM_SIZE:
                        raise NaomiSettingsPatcherException("Found SRAM init section, but it is the wrong size!")

                    # We can just update the data to overwrite this section
                    self.data = self.data[:section.offset] + settings + self.data[(section.offset + section.length):]
                    break
            else:
                # We need to add a SRAM init section to the ROM
                if len(executable.sections) >= 8:
                    raise NaomiSettingsPatcherException("ROM already has the maximum number of init sections!")

                # Add a new section to the end of the rom for this SRAM section
                executable.sections.append(
                    NaomiRomSection(
                        offset=len(self.data),
                        load_address=self.SRAM_LOCATION,
                        length=self.SRAM_SIZE,
                    )
                )
                naomi.main_executable = executable

                # Now, just append it to the end of the file
                self.data = naomi.data + self.data[naomi.HEADER_LENGTH:] + settings

        # Also, write back the new ROM.
        self.__rom = naomi
