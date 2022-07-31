import json
import os
from enum import Enum
from typing import Any, Dict, List, Optional, Union

from arcadeutils import FileBytes
from naomi.eeprom import NaomiEEPRom
from naomi.rom import NaomiRom, NaomiRomRegionEnum
from settings import Settings, SettingsConfig, JSONParseException


class NaomiSettingTypeEnum(Enum):
    UNKNOWN = "unknown"
    SYSTEM = "system"
    GAME = "game"


class NaomiSettingsWrapper:
    # A wrapper class to hold both a system and game settings section together.

    def __init__(self, serial: bytes, system: Settings, game: Settings) -> None:
        self.serial = serial
        self.system = system
        self.game = game

        self.system.type = NaomiSettingTypeEnum.SYSTEM.value
        self.game.type = NaomiSettingTypeEnum.GAME.value

    @staticmethod
    def from_json(settings_files: Dict[str, str], jsondict: Dict[str, Any], context: List[str]) -> "NaomiSettingsWrapper":
        # First, verify that we at least know about the system settings file.
        if "system.settings" not in settings_files:
            raise FileNotFoundError("system.settings does not seem to exist in settings definition directory!")

        # Now, grab the sections that we know we can parse.
        serial = jsondict.get('serial')
        if not isinstance(serial, str) or len(serial) != 4:
            raise JSONParseException(f"\"serial\" key in JSON has invalid data \"{serial}\"!", context)
        serialbytes = serial.encode('ascii')
        gamejson = jsondict.get('game')
        if not isinstance(gamejson, dict):
            raise JSONParseException(f"\"game\" key in JSON has invalid data \"{gamejson}\"!", context)
        systemjson = jsondict.get('system')
        if not isinstance(systemjson, dict):
            raise JSONParseException(f"\"system\" key in JSON has invalid data \"{systemjson}\"!", context)

        # First, load the system settings.
        with open(os.path.join(settings_files["system.settings"]), "r") as fp:
            systemdata = fp.read()
        systemconfig = SettingsConfig.from_data("system.settings", systemdata)

        # Now load the game settings, or if it doesn't exist, default to only allowing system settings to be set.
        gameconfig = NaomiSettingsManager._serial_to_config(settings_files, serialbytes) or SettingsConfig.blank()

        # Finally parse the EEPRom based on the config.
        system = Settings.from_json(systemconfig, systemjson, [*context, "system"], type=NaomiSettingTypeEnum.SYSTEM.value)
        game = Settings.from_json(gameconfig, gamejson, [*context, "game"], type=NaomiSettingTypeEnum.GAME.value)
        return NaomiSettingsWrapper(serialbytes, system, game)

    def to_json(self) -> Dict[str, Any]:
        return {
            'serial': self.serial.decode('ascii'),
            'system': self.system.to_json(),
            'game': self.game.to_json(),
        }

    def __str__(self) -> str:
        return json.dumps(self.to_json(), indent=2)

    def __repr__(self) -> str:
        return str(self)


def get_default_settings_directory() -> str:
    # Specifically for projects including this code as a 3rd-party dependency,
    # look up where we stick the default settings definitions files and return
    # that path as a string, suitable for passing into the "directory" param of
    # SettingsManager.
    return os.path.realpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), "definitions"))


class NaomiSettingsManager:
    # A manager class that can handle manifesting and saving settings given a directory
    # of definition files.

    def __init__(self, directory: str) -> None:
        self.__directory = directory

    @property
    def files(self) -> Dict[str, str]:
        return {
            f: os.path.join(self.__directory, f)
            for f in os.listdir(self.__directory)
            if os.path.isfile(os.path.join(self.__directory, f)) and f.endswith(".settings")
        }

    def files_for_serial(self, serial: bytes) -> Dict[str, str]:
        fnames = {f"{serial.decode('ascii')}.settings", "system.settings"}
        return {f: d for (f, d) in self.files.items() if f in fnames}

    def files_for_rom(self, rom: NaomiRom) -> Dict[str, str]:
        return self.files_for_serial(rom.serial)

    def files_for_eeprom(self, data: Union[bytes, FileBytes]) -> Dict[str, str]:
        eeprom = NaomiEEPRom(data)
        return self.files_for_serial(eeprom.serial)

    @staticmethod
    def _serial_to_config(files: Dict[str, str], serial: bytes) -> Optional[SettingsConfig]:
        fname = f"{serial.decode('ascii')}.settings"

        if fname not in files:
            return None

        with open(files[fname], "r") as fp:
            data = fp.read()

        return SettingsConfig.from_data(fname, data)

    def from_serial(self, serial: bytes) -> NaomiSettingsWrapper:
        config = self._serial_to_config(self.files, serial)
        defaults = None
        if config is not None:
            defaults = config.defaults

        return self.from_eeprom(NaomiEEPRom.default(serial, game_defaults=defaults).data)

    def from_rom(self, rom: NaomiRom, region: NaomiRomRegionEnum) -> NaomiSettingsWrapper:
        # Grab system defaults from ROM header.
        serial = rom.serial
        system_defaults = rom.defaults[region]

        # Grab game defaults from settings file.
        config = self._serial_to_config(self.files, serial)
        game_defaults = None
        if config is not None:
            game_defaults = config.defaults

        # Create a default EEPROM based on both of the above.
        return self.from_eeprom(NaomiEEPRom.default(serial, system_defaults=system_defaults, game_defaults=game_defaults).data)

    def from_eeprom(self, data: Union[bytes, FileBytes]) -> NaomiSettingsWrapper:
        # First grab the parsed EEPRom so we can get the serial.
        eeprom = NaomiEEPRom(data)

        # Now load the system settings.
        with open(os.path.join(self.__directory, "system.settings"), "r") as fp:
            systemdata = fp.read()
        systemconfig = SettingsConfig.from_data("system.settings", systemdata)

        # Now load the game settings, or if it doesn't exist, default to only
        # allowing system settings to be set.
        gameconfig = self._serial_to_config(self.files, eeprom.serial) or SettingsConfig.blank()

        # Finally parse the EEPRom based on the config.
        system = Settings.from_config(systemconfig, eeprom.system.data, type=NaomiSettingTypeEnum.SYSTEM.value)
        game = Settings.from_config(gameconfig, eeprom.game.data, type=NaomiSettingTypeEnum.GAME.value)
        return NaomiSettingsWrapper(eeprom.serial, system, game)

    def from_json(self, jsondict: Dict[str, Any], context: Optional[List[str]] = None) -> NaomiSettingsWrapper:
        return NaomiSettingsWrapper.from_json(self.files, jsondict, context or [])

    def to_eeprom(self, settings: NaomiSettingsWrapper) -> bytes:
        # First, create the EEPROM.
        eeprom = NaomiEEPRom.default(settings.serial)

        # Now, update the game length.
        eeprom.length = settings.game.length

        for section, settingsgroup in [
            (eeprom.system, settings.system),
            (eeprom.game, settings.game),
        ]:
            if not section.valid:
                # If we couldn't make this section correct, completely skip out on it.
                continue

            section.data = settingsgroup.to_bytes()

        return eeprom.data
