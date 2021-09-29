#! /usr/bin/env python3
import struct
from typing import Optional, Tuple

from naomi import NaomiRom, NaomiRomSection, NaomiEEPRom


class NaomiSettingsPatcherException(Exception):
    pass


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
    def __get_config(data: bytes) -> Tuple[int, int, bool, bool]:
        # Returns a tuple consisting of the original EXE start address and
        # the desired trojan start address, whether sentinel mode is enabled
        # and whether debug printing is enabled.
        for i in range(len(data) - 24):
            if all(x == 0xEE for x in data[i:(i + 4)]) and all(x == 0xEE for x in data[(i + 20):(i + 24)]):
                original_start, trojan_start, sentinel, debug = struct.unpack("<IIII", data[(i + 4):(i + 20)])
                return (
                    original_start,
                    trojan_start,
                    sentinel != 0,
                    debug != 0,
                )

        raise NaomiSettingsPatcherException("Couldn't find config in executable!")

    def get_settings(self) -> Optional[bytes]:
        # Parse the ROM header so we can narrow our search.
        naomi = NaomiRom(self.data)

        # Only look at main executables.
        executable = naomi.main_executable
        for sec in executable.sections:
            try:
                # Grab the old entrypoint from the existing modification since the ROM header
                # entrypoint will be the old trojan EXE.
                data = self.data[sec.offset:(sec.offset + sec.length)]
                entrypoint, _, _, _ = self.__get_config(data)

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
        _, location, _, _ = self.__get_config(exe)

        for sec in executable.sections:
            if sec.load_address == location:
                # Grab the old entrypoint from the existing modification since the ROM header
                # entrypoint will be the old trojan EXE.
                entrypoint, _, _, _ = self.__get_config(data[sec.offset:(sec.offset + sec.length)])
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
