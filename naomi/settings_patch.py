#! /usr/bin/env python3
import os
import struct
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


class NaomiSettingsPatcher:
    def __init__(self, rom: bytes, trojan: bytes) -> None:
        self.data = rom
        self.__trojan = trojan

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

    def get_serial(self) -> bytes:
        # Parse the ROM header so we can grab the game serial code.
        naomi = NaomiRom(self.data)
        return naomi.serial

    def get_info(self) -> Optional[NaomiSettingsInfo]:
        # Parse the ROM header so we can narrow our search.
        naomi = NaomiRom(self.data)

        # Only look at main executables.
        executable = naomi.main_executable
        for sec in executable.sections:
            # Constrain the search to the section that we jump to, since that will always
            # be where our trojan is.
            if executable.entrypoint >= sec.load_address and executable.entrypoint < (sec.load_address + sec.length):
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
        naomi = NaomiRom(self.data)

        # Only look at main executables.
        executable = naomi.main_executable
        for sec in executable.sections:
            # Constrain the search to the section that we jump to, since that will always
            # be where our trojan is.
            if executable.entrypoint >= sec.load_address and executable.entrypoint < (sec.load_address + sec.length):
                try:
                    # Grab the old entrypoint from the existing modification since the ROM header
                    # entrypoint will be the old trojan EXE.
                    data = self.data[sec.offset:(sec.offset + sec.length)]
                    self.__get_config(data)

                    # Returns the requested EEPRom settings that should be written prior
                    # to the game starting.
                    for i in range(len(data) - 128):
                        if NaomiEEPRom.validate(data[i:(i + 128)]):
                            return data[i:(i + 128)]
                except Exception:
                    pass

        # Couldn't find a section that matched.
        return None

    def put_settings(self, settings: bytes, *, enable_sentinel: bool = False, enable_debugging: bool = False, verbose: bool = False) -> None:
        # First, parse the ROM we were given.
        data = self.data
        naomi = NaomiRom(data)

        # First, we need to modify the settings trojan with this ROM's load address and
        # the EEPROM we want to add. Make sure the EEPRom we were given is valid.
        if len(settings) != 128:
            raise NaomiSettingsPatcherException("Invalid length of settings!")
        if not NaomiEEPRom.validate(settings):
            raise NaomiSettingsPatcherException("Settings is incorrectly formed!")
        if naomi.serial != settings[3:7] or naomi.serial != settings[21:25]:
            raise NaomiSettingsPatcherException("Settings is not for this game!")

        # Now we need to add an EXE init section to the ROM.
        exe = self.__trojan[:]
        executable = naomi.main_executable
        _, location, _, _, _ = self.__get_config(exe)

        for sec in executable.sections:
            if sec.load_address == location:
                # Grab the old entrypoint from the existing modification since the ROM header
                # entrypoint will be the old trojan EXE.
                entrypoint, _, _, _, _ = self.__get_config(data[sec.offset:(sec.offset + sec.length)])
                exe = self.__patch_bytesequence(exe, 0xAA, struct.pack("<I", entrypoint))
                exe = self.__patch_bytesequence(exe, 0xBB, settings)
                exe = self.__patch_bytesequence(exe, 0xCF, struct.pack("<I", 1 if enable_sentinel else 0))
                exe = self.__patch_bytesequence(exe, 0xDD, struct.pack("<I", 1 if enable_debugging else 0))

                # We can reuse this section, but first we need to get rid of the old patch.
                if sec.offset + sec.length == len(data):
                    # We can just stick the new file right on top of where the old was.
                    if verbose:
                        print("Overwriting old settings in existing ROM section.")

                    # Cut off the old section, add our new section, make sure the length is correct.
                    data = data[:sec.offset] + exe
                    sec.length = len(exe)
                else:
                    # It is somewhere in the middle of an executable, zero it out and
                    # then add this section to the end of the ROM.
                    if verbose:
                        print("Zeroing out old settings in existing ROM section and attaching new settings to the end of the file.")

                    # Patch the executable with the correct settings and entrypoint.
                    data = self.__change(data, b"\0" * sec.length, sec.offset)
                    sec.offset = len(data)
                    sec.length = len(exe)
                    sec.load_address = location
                    data += exe
                break
        else:
            if len(executable.sections) >= 8:
                raise NaomiSettingsPatcherException("ROM already has the maximum number of init sections!")

            # Add a new section to the end of the rom for this binary data.
            if verbose:
                print("Attaching settings to a new ROM section at the end of the file.")

            executable.sections.append(
                NaomiRomSection(
                    offset=len(data),
                    load_address=location,
                    length=len(exe),
                )
            )

            # Patch the executable with the correct settings and entrypoint.
            exe = self.__patch_bytesequence(exe, 0xAA, struct.pack("<I", executable.entrypoint))
            exe = self.__patch_bytesequence(exe, 0xBB, settings)
            exe = self.__patch_bytesequence(exe, 0xCF, struct.pack("<I", 1 if enable_sentinel else 0))
            exe = self.__patch_bytesequence(exe, 0xDD, struct.pack("<I", 1 if enable_debugging else 0))
            data += exe

        executable.entrypoint = location

        # Generate new header and attach executable to end of data.
        naomi.main_executable = executable

        # Now, save this back so it can be read by other callers.
        self.data = naomi.data + data[naomi.HEADER_LENGTH:]
