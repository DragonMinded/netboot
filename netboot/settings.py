import os
import os.path
import threading

from typing import Any, Dict, List, Optional, Tuple
from arcadeutils import FileBytes, BinaryDiff, BinaryDiffException
from naomi import NaomiRom, NaomiRomRegionEnum, NaomiSettingsPatcher, get_default_trojan
from naomi.settings import NaomiSettingsWrapper, NaomiSettingsManager


class SettingsException(Exception):
    pass


class SettingsManager:
    def __init__(self, naomi_directory: str) -> None:
        self.__naomi_directory = naomi_directory
        self.__naomi_manager = NaomiSettingsManager(naomi_directory)
        self.__lock: threading.Lock = threading.Lock()
        self.__cache: Dict[str, List[str]] = {}

    @property
    def naomi_directory(self) -> str:
        return self.__naomi_directory

    def get_naomi_settings(
        self,
        filename: str,
        settingsdata: Optional[bytes],
        region: NaomiRomRegionEnum = NaomiRomRegionEnum.REGION_JAPAN,
        patches: Optional[List[str]] = None,
    ) -> Tuple[Optional[NaomiSettingsWrapper], bool]:
        settings: Optional[NaomiSettingsWrapper] = None
        patches = patches or []
        with open(filename, "rb") as fp:
            data = FileBytes(fp)

            # Check to make sure its not already got an SRAM section. If it
            # does, disallow the following section from being created.
            patcher = NaomiSettingsPatcher(data, get_default_trojan())

            # First, try to load any previously configured EEPROM.
            if settingsdata is not None:
                if len(settingsdata) != NaomiSettingsPatcher.EEPROM_SIZE:
                    raise Exception("We don't support non-EEPROM settings!")

                settings = self.__naomi_manager.from_eeprom(settingsdata)
            else:
                # Second, if we didn't configure one, see if there's a previously configured
                # one in the ROM itself.
                settingsdata = patcher.get_eeprom()
                if settingsdata is not None:
                    if len(settingsdata) != NaomiSettingsPatcher.EEPROM_SIZE:
                        raise Exception("We don't support non-EEPROM settings!")

                    settings = self.__naomi_manager.from_eeprom(settingsdata)
                else:
                    # Finally, attempt to patch with any patches that fit in the first
                    # chunk, so the defaults we get below match any force settings
                    # patches we did to the header.
                    for patch in patches:
                        with open(patch, "r") as pp:
                            differences = pp.readlines()
                        differences = [d.strip() for d in differences if d.strip()]
                        try:
                            data = BinaryDiff.patch(data, differences, ignore_size_differences=True)
                        except BinaryDiffException:
                            # Patch was for something not in the header.
                            pass
                    rom = NaomiRom(data)
                    if rom.valid:
                        settings = self.__naomi_manager.from_rom(rom, region)

        return settings, settingsdata is not None

    def put_naomi_settings(self, settings: Dict[str, Any]) -> bytes:
        parsedsettings = self.__naomi_manager.from_json(settings)
        return self.__naomi_manager.to_eeprom(parsedsettings)

    def __directories(self) -> List[str]:
        return [self.__naomi_directory]

    @property
    def directories(self) -> List[str]:
        with self.__lock:
            return self.__directories()

    def settings(self, directory: str) -> List[str]:
        with self.__lock:
            if directory not in self.__directories():
                raise SettingsException(f"Directory {directory} is not managed by us!")
            return sorted([f for f in os.listdir(directory) if os.path.isfile(os.path.join(directory, f))])

    def recalculate(self, filename: Optional[str] = None) -> None:
        with self.__lock:
            if filename is None:
                self.__cache = {}
            else:
                if filename in self.__cache:
                    del self.__cache[filename]

    def settings_for_game(self, filename: str) -> List[str]:
        with self.__lock:
            # First, see if we already cached this file.
            if filename in self.__cache:
                return self.__cache[filename]

            valid_settings: List[str] = []

            # Now, try to treat it as a Naomi ROM
            with open(filename, "rb") as fp:
                data = FileBytes(fp)
                rom = NaomiRom(data)
                if rom.valid:
                    valid_settings = sorted([f for f, _ in self.__naomi_manager.files_for_rom(rom).items()])

            self.__cache[filename] = valid_settings
            return valid_settings
