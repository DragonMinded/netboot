import json
import os
import struct
from enum import Enum, auto
from typing import Any, Dict, List, Optional, Tuple, Union

from naomi.eeprom import NaomiEEPRom


class SettingSizeEnum(Enum):
    UNKNOWN = auto()
    NIBBLE = auto()
    BYTE = auto()


class SettingType(Enum):
    UNKNOWN = auto()
    SYSTEM = auto()
    GAME = auto()


class Condition:
    # A wrapper class to encapsulate that a setting is read-only based on the
    # value of another setting.

    def __init__(self, name: str, values: List[int], negate: bool) -> None:
        self.name = name
        self.values = values
        self.negate = negate

    def evaluate(self, settings: List["Setting"]) -> bool:
        for setting in settings:
            if setting.name == self.name:
                if setting.current in self.values:
                    return self.negate
                else:
                    return not self.negate
        return False


class Setting:
    # A single setting, complete with its name, size (and optional length if
    # the size is a byte), whether it is read-only, the allowed values for
    # the setting and finally the current value (if it has been parsed out
    # of a valid EEPROM file).

    def __init__(
        self,
        name: str,
        size: SettingSizeEnum,
        length: int,
        read_only: Union[bool, Condition],
        values: Optional[Dict[int, str]] = None,
        current: Optional[int] = None,
    ) -> None:
        self.name = name
        self.size = size
        self.length = length
        self.read_only = read_only
        self.values = values or {}
        self.current = current

        if size == SettingSizeEnum.UNKNOWN:
            raise Exception("Logic error!")
        if length > 1 and size != SettingSizeEnum.BYTE:
            raise Exception("Logic error!")

    def to_json(self) -> Dict[str, Any]:
        jdict = {
            'name': self.name,
            'size': self.size.name,
            'length': self.length,
            'values': self.values,
            'current': self.current,
        }

        if self.read_only is True:
            jdict['readonly'] = True
        elif self.read_only is False:
            jdict['readonly'] = False
        elif isinstance(self.read_only, Condition):
            jdict['condition'] = {
                "name": self.read_only.name,
                "values": self.read_only.values,
                "negate": self.read_only.negate,
            }
        return jdict

    def __str__(self) -> str:
        return json.dumps(self.to_json(), indent=2)


class Settings:
    # A collection of settings as well as the type of settings this is (game versus
    # system). This is also responsible for parsing and creating sections in an actual
    # EEPROM file based on the settings themselves.

    def __init__(self, settings: List[Setting], type: SettingType = SettingType.UNKNOWN) -> None:
        self.settings = settings
        self.type = type

    @staticmethod
    def from_config(type: SettingType, config: "SettingsConfig", eeprom: NaomiEEPRom) -> "Settings":
        settings = config.settings
        location = 0
        halves = 0

        if type == SettingType.SYSTEM:
            data = eeprom.system
        elif type == SettingType.GAME:
            data = eeprom.game
        else:
            raise Exception(f"Cannot load settings with a config of type {type.name}!")

        for setting in settings:
            if setting.size == SettingSizeEnum.NIBBLE:
                if halves == 0:
                    setting.current = (data[location] >> 4) & 0xF
                else:
                    setting.current = data[location] & 0xF

                if halves == 0:
                    halves = 1
                else:
                    halves = 0
                    location += 1
            elif setting.size == SettingSizeEnum.BYTE:
                if halves != 0:
                    raise Exception("Logic error!")
                if setting.length == 1:
                    setting.current = struct.unpack("<B", data[location:(location + 1)])[0]
                elif setting.length == 2:
                    setting.current = struct.unpack("<H", data[location:(location + 2)])[0]
                elif setting.length == 4:
                    setting.current = struct.unpack("<I", data[location:(location + 4)])[0]
                else:
                    raise Exception(f"Cannot convert setting of length {setting.length}!")

                location += setting.length

        return Settings(settings, type=type)

    def to_json(self) -> Dict[str, Any]:
        return {
            'type': self.type.name,
            'settings': [
                setting.to_json() for setting in self.settings
            ],
        }

    def __str__(self) -> str:
        return json.dumps(self.to_json(), indent=2)


class SettingsWrapper:
    # A wrapper class to hold both a system and game settings section together.

    def __init__(self, system: Settings, game: Settings) -> None:
        self.system = system
        self.game = game

        self.system.type = SettingType.SYSTEM
        self.game.type = SettingType.GAME

    def to_json(self) -> Dict[str, Any]:
        return {
            'system': self.system.to_json(),
            'game': self.game.to_json(),
        }

    def __str__(self) -> str:
        return json.dumps(self.to_json(), indent=2)


class SettingsConfig:
    # A class that can manifest a list of settings given a particular
    # file. It is not responsible for parsing any settings. It is only
    # responsible for creating the list of settings given a settings
    # definition file.

    def __init__(self, settings: List[Setting]) -> None:
        self.settings = settings

    @staticmethod
    def blank() -> "SettingsConfig":
        return SettingsConfig([])

    @staticmethod
    def __get_kv(setting: str) -> Tuple[int, str]:
        if "-" in setting:
            k, v = setting.split("-", 1)
            key = int(k.strip(), 16)
            value = v.strip()
        else:
            key = int(setting.strip(), 16)
            value = f"{key}"

        return key, value

    @staticmethod
    def __get_vals(setting: str) -> Tuple[str, List[int]]:
        name, rest = setting.split(" is ", 1)
        name = name.strip()
        vals: List[int] = []

        for val in rest.split(" or "):
            vals.append(int(val, 16))

        return name, vals

    @staticmethod
    def from_data(data: str) -> "SettingsConfig":
        lines = data.splitlines()
        settings: List[Setting] = []

        cur_setting: Optional[Setting] = None
        for line in lines:
            # Skip blank lines.
            if not line.strip():
                continue

            if ":" not in line:
                # Assume that this is a setting entry.
                if cur_setting is None:
                    raise Exception("Setting entry without a parent setting!")
                k, v = SettingsConfig.__get_kv(line)
                cur_setting.values[k] = v
            else:
                # Assume that this is a setting.
                if cur_setting:
                    settings.append(cur_setting)
                    cur_setting = None

                # First, get the name as well as the size and any restrictions.
                name, rest = line.split(":", 1)
                name = name.strip()
                rest = rest.strip()

                # Now, figure out what size it should be.
                size = SettingSizeEnum.UNKNOWN
                length = 1
                read_only: Union[bool, Condition] = False
                values: Dict[int, str] = {}

                if "," in rest:
                    restbits = [r.strip() for r in rest.split(",")]
                else:
                    restbits = [rest]

                for bit in restbits:
                    if "byte" in bit or "nibble" in bit:
                        if " " in bit:
                            lenstr, units = bit.split(" ", 1)
                            length = int(lenstr.strip())
                            units = units.strip()
                        else:
                            units = bit.strip()

                        if "byte" in units:
                            size = SettingSizeEnum.BYTE
                        elif "nibble" in units:
                            size = SettingSizeEnum.NIBBLE
                        else:
                            raise Exception(f"Unrecognized unit {units}!")
                        if size != SettingSizeEnum.BYTE and length != 1:
                            raise Exception(f"Invalid length for unit {units}!")
                    elif "read-only" in bit:
                        if " if " in bit:
                            readonlystr, rest = bit.split(" if ", 1)
                            if readonlystr.strip() != "read-only":
                                raise Exception(f"Cannot parse read-only condition {bit}!")
                            condname, condvalues = SettingsConfig.__get_vals(rest)
                            read_only = Condition(condname, condvalues, True)
                        elif " unless " in bit:
                            readonlystr, rest = bit.split(" unless ", 1)
                            if readonlystr.strip() != "read-only":
                                raise Exception(f"Cannot parse read-only condition {bit}!")
                            condname, condvalues = SettingsConfig.__get_vals(rest)
                            read_only = Condition(condname, condvalues, False)
                        else:
                            # Its unconditionally read-only.
                            read_only = True
                    else:
                        # Assume this is a setting value.
                        k, v = SettingsConfig.__get_kv(bit)
                        values[k] = v

                cur_setting = Setting(
                    name,
                    size,
                    length,
                    read_only,
                    values,
                )

        if cur_setting:
            settings.append(cur_setting)
            cur_setting = None

        # Verify that nibbles come in pairs.
        halves = 0
        for setting in settings:
            if setting.size == SettingSizeEnum.NIBBLE:
                halves = 1 - halves
            elif setting.size == SettingSizeEnum.BYTE:
                if halves != 0:
                    raise Exception(f"Setting {setting.name} comes after a single nibble, but it needs to be byte-aligned!")

        return SettingsConfig(settings)

    def defaults(self) -> bytes:
        raise Exception("TODO!")


class SettingsManager:
    # A manager class that can handle manifesting and saving settings given a directory
    # of definition files.

    def __init__(self, directory: str) -> None:
        self.__directory = directory

    def __serial_to_config(self, serial: bytes) -> Optional[SettingsConfig]:
        files = {f: os.path.join(self.__directory, f) for f in os.listdir(self.__directory) if os.path.isfile(os.path.join(self.__directory, f))}
        fname = f"{serial.decode('ascii')}.settings"

        if fname not in files:
            return None

        with open(files[fname], "r") as fp:
            data = fp.read()

        return SettingsConfig.from_data(data)

    def from_serial(self, serial: bytes) -> SettingsWrapper:
        config = self.__serial_to_config(serial)
        defaults = None
        if config is not None:
            defaults = config.defaults()

        return self.from_eeprom(NaomiEEPRom.default(serial, game_defaults=defaults).data)

    def from_eeprom(self, data: bytes) -> SettingsWrapper:
        # First grab the parsed EEPRom so we can get the serial.
        eeprom = NaomiEEPRom(data)

        # Now load the system settings.
        with open(os.path.join(self.__directory, "system.settings"), "r") as fp:
            systemdata = fp.read()
        systemconfig = SettingsConfig.from_data(systemdata)

        # Now load the game settings, or if it doesn't exist, default to only
        # allowing system settings to be set.
        gameconfig = self.__serial_to_config(eeprom.serial) or SettingsConfig.blank()

        # Finally parse the EEPRom based on the config.
        system = Settings.from_config(SettingType.SYSTEM, systemconfig, eeprom)
        game = Settings.from_config(SettingType.GAME, gameconfig, eeprom)
        return SettingsWrapper(system, game)
