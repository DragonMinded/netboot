import base64
import json
import struct
from enum import Enum, auto
from typing import Any, Dict, List, Optional, Tuple, Union


class SettingSizeEnum(Enum):
    UNKNOWN = auto()
    NIBBLE = auto()
    BYTE = auto()


NO_FILE: str = "NO FILE"


class SettingsParseException(Exception):
    def __init__(self, msg: str, filename: str) -> None:
        super().__init__(msg)
        self.filename = filename


class SettingsSaveException(Exception):
    def __init__(self, msg: str, filename: str) -> None:
        super().__init__(msg)
        self.filename = filename


class JSONParseException(Exception):
    def __init__(self, msg: str, context: List[str]) -> None:
        super().__init__(msg)
        self.context = context


class ReadOnlyCondition:
    # A wrapper class to encapsulate that a setting is read-only based on the
    # value of another setting.

    def __init__(self, filename: str, setting: str, name: str, values: List[int], negate: bool) -> None:
        self.filename = filename
        self.setting = setting
        self.name = name
        self.values = values
        self.negate = negate

    def evaluate(self, settings: List["Setting"]) -> bool:
        for setting in settings:
            if setting.name.lower() == self.name.lower():
                if (setting.current if setting.current is not None else setting.default) in self.values:
                    return self.negate
                else:
                    return not self.negate

        raise SettingsSaveException(
            f"The setting \"{self.setting}\" depends on the value for \"{self.name}\" but that setting does not seem to exist! Perhaps you misspelled \"{self.name}\"?",
            self.filename,
        )

    def __eq__(self, other: object) -> bool:
        if other is self:
            return True
        if not isinstance(other, ReadOnlyCondition):
            return False

        return (
            self.filename == other.filename and
            self.setting == other.setting and
            self.name == other.name and
            self.values == other.values and
            self.negate == other.negate
        )

    def __ne__(self, other: object) -> bool:
        # Python documentation recommends doing this instead of explicitly calling __eq__.
        return not self == other


class DefaultCondition:
    # A wrapper class to encapsulate one rule for a setting default.

    def __init__(self, name: str, values: List[int], negate: bool, default: int) -> None:
        self.name = name
        self.values = values
        self.negate = negate
        self.default = default

    def __eq__(self, other: object) -> bool:
        if other is self:
            return True
        if not isinstance(other, DefaultCondition):
            return False

        return (
            self.name == other.name and
            self.values == other.values and
            self.negate == other.negate and
            self.default == other.default
        )

    def __ne__(self, other: object) -> bool:
        # Python documentation recommends doing this instead of explicitly calling __eq__.
        return not self == other


class DefaultConditionGroup:
    # A wrapper class to encapsulate a set of rules for defaulting a setting.

    def __init__(self, filename: str, setting: str, conditions: List[DefaultCondition]) -> None:
        self.filename = filename
        self.setting = setting
        self.conditions = conditions

    def __eq__(self, other: object) -> bool:
        if other is self:
            return True
        if not isinstance(other, DefaultConditionGroup):
            return False

        return (
            self.filename == other.filename and
            self.setting == other.setting and
            self.conditions == other.conditions
        )

    def __ne__(self, other: object) -> bool:
        # Python documentation recommends doing this instead of explicitly calling __eq__.
        return not self == other

    def evaluate(self, settings: List["Setting"]) -> int:
        for cond in self.conditions:
            for setting in settings:
                if setting.name.lower() == cond.name.lower():
                    current = setting.current if setting.current is not None else setting.default

                    if cond.negate and current not in cond.values:
                        return cond.default
                    if not cond.negate and current in cond.values:
                        return cond.default

        namelist = list({f'"{c.name}"' for c in self.conditions})
        if len(namelist) > 2:
            namelist = [", ".join(namelist[:-1]), namelist[-1]]
        names = " or ".join(namelist)

        raise SettingsSaveException(
            f"The default for setting \"{self.setting}\" could not be determined! Perhaps you misspelled one of {names}, or you forgot a value?",
            self.filename,
        )


class Setting:
    # A single setting, complete with its name, size (and optional length if
    # the size is a byte), whether it is read-only, the allowed values for
    # the setting and finally the current value (if it has been parsed out
    # of a valid EEPROM file).

    def __init__(
        self,
        name: str,
        order: int,
        size: SettingSizeEnum,
        length: int,
        read_only: Union[bool, ReadOnlyCondition],
        values: Optional[Dict[int, str]] = None,
        current: Optional[int] = None,
        default: Optional[Union[int, DefaultConditionGroup]] = None,
    ) -> None:
        self.name = name
        self.order = order
        self.size = size
        self.length = length
        self.read_only = read_only
        self.values = values or {}
        self.current = current
        self.default = default

        if size == SettingSizeEnum.UNKNOWN:
            raise Exception("Logic error!")
        if length > 1 and size != SettingSizeEnum.BYTE:
            raise Exception("Logic error!")
        if order < 0:
            raise Exception("Logic error!")

    @staticmethod
    def from_json(filename: str, jsondict: Dict[str, Any], context: List[str]) -> "Setting":
        # Convert JSON dictionary back to a Setting class.
        name = jsondict.get('name')
        if not isinstance(name, str):
            raise JSONParseException(f"\"name\" key in JSON has invalid data \"{name}\"!", context)

        sizestr = jsondict.get('size')
        if not isinstance(sizestr, str):
            raise JSONParseException(f"\"size\" key in JSON has invalid data \"{sizestr}\"!", context)
        try:
            size = SettingSizeEnum[sizestr]
        except KeyError:
            size = SettingSizeEnum.UNKNOWN
        if size == SettingSizeEnum.UNKNOWN:
            raise JSONParseException(f"\"size\" key in JSON has invalid data \"{sizestr}\"!", context)

        length = jsondict.get('length')
        if not isinstance(length, int):
            # Try converting from string.
            try:
                length = int(length)  # type: ignore
            except ValueError:
                pass

            if not isinstance(length, int):
                raise JSONParseException(f"\"length\" key in JSON has invalid data \"{length}\"!", context)

        order = jsondict.get('order')
        if not isinstance(order, int):
            # Try converting from string.
            try:
                order = int(order)  # type: ignore
            except ValueError:
                pass

            if not isinstance(order, int):
                raise JSONParseException(f"\"order\" key in JSON has invalid data \"{order}\"!", context)

        current = jsondict.get('current')
        if current is not None and not isinstance(current, int):
            # Try converting from string.
            try:
                current = int(current)
            except ValueError:
                pass

            if not isinstance(current, int):
                raise JSONParseException(f"\"current\" key in JSON has invalid data \"{current}\"!", context)

        valuedict = jsondict.get('values')
        values: Optional[Dict[int, str]] = None
        if valuedict is not None:
            if not isinstance(valuedict, dict):
                raise JSONParseException(f"\"values\" key in JSON has invalid data \"{valuedict}\"!", context)
            try:
                values = {int(k): str(v) for (k, v) in valuedict.items()}
            except ValueError:
                pass
            if values is None:
                raise JSONParseException(f"\"values\" key in JSON has invalid data \"{valuedict}\"!", context)

        readonlydict = jsondict.get('readonly')
        readonly: Union[bool, ReadOnlyCondition]
        if readonlydict is True:
            readonly = True
        elif readonlydict is False:
            readonly = False
        elif isinstance(readonlydict, dict):
            # Parse out conditional.
            roname = readonlydict.get('name')
            if not isinstance(roname, str):
                raise JSONParseException(f"\"name\" key in JSON has invalid data \"{roname}\"!", [*context, "readonly"])

            rovaluelist = readonlydict.get('values')
            if not isinstance(rovaluelist, list):
                raise JSONParseException(f"\"values\" key in JSON has invalid data \"{rovaluelist}\"!", [*context, "readonly"])
            rovalues = None
            try:
                rovalues = [int(k) for k in rovaluelist]
            except ValueError:
                pass
            if rovalues is None:
                raise JSONParseException(f"\"values\" key in JSON has invalid data \"{rovaluelist}\"!", [*context, "readonly"])

            ronegate = readonlydict.get('negate')
            if not isinstance(ronegate, bool):
                raise JSONParseException(f"\"negate\" key in JSON has invalid data \"{ronegate}\"!", [*context, "readonly"])

            readonly = ReadOnlyCondition(filename, name, roname, rovalues, ronegate)
        else:
            raise JSONParseException(f"\"readonly\" key in JSON has invalid data \"{readonlydict}\"!", context)

        defaultdict = jsondict.get('default')
        default: Optional[Union[int, DefaultConditionGroup]] = None
        if defaultdict is None or isinstance(defaultdict, int):
            default = defaultdict
        elif isinstance(defaultdict, str):
            try:
                default = int(defaultdict)
            except ValueError:
                default = None

            if default is None:
                raise JSONParseException(f"\"default\" key in JSON has invalid data \"{defaultdict}\"!", context)
        elif isinstance(defaultdict, list):
            # Go through each entry in the list and parse out the conditionals.
            default = DefaultConditionGroup(filename, name, [])

            for i, defaultblob in enumerate(defaultdict):
                # Parse out conditional.
                defname = defaultblob.get('name')
                if not isinstance(defname, str):
                    raise JSONParseException(f"\"name\" key in JSON has invalid data \"{defname}\"!", [*context, f"default[{i}]"])

                defvaluelist = defaultblob.get('values')
                if not isinstance(defvaluelist, list):
                    raise JSONParseException(f"\"values\" key in JSON has invalid data \"{defvaluelist}\"!", [*context, f"default[{i}]"])
                defvalues = None
                try:
                    defvalues = [int(k) for k in defvaluelist]
                except ValueError:
                    pass
                if defvalues is None:
                    raise JSONParseException(f"\"values\" key in JSON has invalid data \"{defvaluelist}\"!", [*context, f"default[{i}]"])

                defnegate = defaultblob.get('negate')
                if not isinstance(defnegate, bool):
                    raise JSONParseException(f"\"negate\" key in JSON has invalid data \"{defnegate}\"!", [*context, f"default[{i}]"])

                defdefault = defaultblob.get('default')
                if not isinstance(defdefault, int):
                    # Try converting from string.
                    try:
                        defdefault = int(defdefault)
                    except ValueError:
                        pass

                    if not isinstance(defdefault, int):
                        raise JSONParseException(f"\"default\" key in JSON has invalid data \"{defdefault}\"!", [*context, f"default[{i}]"])

                default.conditions.append(DefaultCondition(defname, defvalues, defnegate, defdefault))
        else:
            raise JSONParseException(f"\"default\" key in JSON has invalid data \"{defaultdict}\"!", context)

        return Setting(
            name,
            order,
            size,
            length,
            readonly,
            values,
            current,
            default,
        )

    def to_json(self) -> Dict[str, Any]:
        jdict = {
            'name': self.name,
            'order': self.order,
            'size': self.size.name,
            'length': self.length,
            'values': self.values,
            'current': self.current,
        }

        if self.read_only is True:
            jdict['readonly'] = True
        elif self.read_only is False:
            jdict['readonly'] = False
        elif isinstance(self.read_only, ReadOnlyCondition):
            jdict['readonly'] = {
                "name": self.read_only.name,
                "values": self.read_only.values,
                "negate": self.read_only.negate,
            }

        if self.default is None or isinstance(self.default, int):
            jdict['default'] = self.default
        elif isinstance(self.default, DefaultConditionGroup):
            jdict['default'] = [
                {
                    "name": cond.name,
                    "values": cond.values,
                    "default": cond.default,
                    "negate": cond.negate,
                }
                for cond in self.default.conditions
            ]
        return jdict

    def __str__(self) -> str:
        return json.dumps(self.to_json(), indent=2)

    def __repr__(self) -> str:
        return str(self)


class Settings:
    # A collection of settings as well as the type of settings this is. This is also responsible
    # for parsing and creating sections in an actual EEPROM file based on the settings themselves.
    # If your settings doesn't have a type (for instance if it is the only collection of settings
    # for a given file format), you can leave this unset. Default endianness is LE, but if you want
    # big endian, you can specify that as well.

    def __init__(self, filename: str, settings: List[Setting], type: Optional[str] = None, big_endian: bool = False) -> None:
        self.filename = filename
        self.settings = settings
        self.type = type
        self.endian = ">" if big_endian else "<"

    @staticmethod
    def from_config(config: "SettingsConfig", data: bytes, type: Optional[str] = None, big_endian: bool = False) -> "Settings":
        # Keep track of how many bytes into the EEPROM we are.
        location = 0
        halves = 0
        halfname: Optional[str] = None
        endian = ">" if big_endian else "<"

        # We need to make sure this is in load order, so that we can parse out the
        # correct settings in order of them showing up in the file.
        for setting in sorted(config.settings, key=lambda setting: setting.order):
            if setting.size == SettingSizeEnum.NIBBLE:
                if location < len(data):
                    if halves == 0:
                        setting.current = (data[location] >> 4) & 0xF
                    else:
                        setting.current = data[location] & 0xF

                if halves == 0:
                    halfname = setting.name
                    halves = 1
                else:
                    halfname = None
                    halves = 0
                    location += 1
            elif setting.size == SettingSizeEnum.BYTE:
                if halves != 0:
                    raise SettingsParseException(
                        f"The setting \"{setting.name}\" follows a lonesome half-byte setting \"{halfname}\". Half-byte settings must always be in pairs!",
                        config.filename
                    )

                if setting.length == 1:
                    if location < len(data):
                        setting.current = struct.unpack(endian + "B", data[location:(location + 1)])[0]
                elif setting.length == 2:
                    if location < (len(data) - 1):
                        setting.current = struct.unpack(endian + "H", data[location:(location + 2)])[0]
                elif setting.length == 4:
                    if location < (len(data) - 3):
                        setting.current = struct.unpack(endian + "I", data[location:(location + 4)])[0]
                else:
                    raise SettingsParseException(f"Cannot parse setting \"{setting.name}\" with unrecognized size \"{setting.length}\"!", config.filename)

                location += setting.length

        if halves != 0:
            raise SettingsSaveException(
                f"The setting \"{halfname}\" is a lonesome half-byte setting. Half-byte settings must always be in pairs!",
                config.filename
            )

        # Return this in the original order presented to us.
        final_settings = Settings(config.filename, config.settings, type=type, big_endian=big_endian)
        if final_settings.length != len(data):
            raise SettingsParseException(
                f"Unexpected final size of {type if type is not None else 'unnamed'} section, expected {len(data)} bytes but definition file covers {final_settings.length} bytes!",
                config.filename,
            )
        return final_settings

    @property
    def length(self) -> int:
        # Note that this is different than just len(self.settings), because some settings can take up
        # more or less than a single byte. This calculates the length of the data in bytes that will
        # be returned by to_bytes()
        halves = 0
        length = 0
        halfname: Optional[str] = None

        for setting in sorted(self.settings, key=lambda setting: setting.order):
            if setting.size == SettingSizeEnum.NIBBLE:
                # Update our length.
                if halves == 0:
                    halfname = setting.name
                    halves = 1
                else:
                    halfname = None
                    halves = 0
                    length += 1

            elif setting.size == SettingSizeEnum.BYTE:
                # First, make sure we aren't in a pending nibble state.
                if halves != 0:
                    raise SettingsSaveException(
                        f"The setting \"{setting.name}\" follows a lonesome half-byte setting \"{halfname}\". Half-byte settings must always be in pairs!",
                        self.filename
                    )

                if setting.length not in {1, 2, 4}:
                    raise SettingsSaveException(f"Cannot save setting \"{setting.name}\" with unrecognized size {setting.length}!", self.filename)

                # Update our length.
                length += setting.length

        if halves != 0:
            raise SettingsSaveException(
                f"The setting \"{halfname}\" is a lonesome half-byte setting. Half-byte settings must always be in pairs!",
                self.filename
            )

        return length

    @staticmethod
    def from_json(
        config: "SettingsConfig",
        jsondict: Dict[str, Any],
        context: List[str],
        type: Optional[str] = None,
        big_endian: bool = False,
    ) -> "Settings":
        # First, parse out the keys that we know we need.
        if type is not None:
            typestr = jsondict.get('type')
            if not isinstance(typestr, str) or typestr != type:
                raise JSONParseException(f"\"type\" key in JSON has invalid data \"{typestr}\"!", context)
        settings = jsondict.get('settings')
        if not isinstance(settings, list):
            raise JSONParseException(f"\"settings\" key in JSON has invalid data \"{settings}\"!", context)
        filename = jsondict.get('filename')
        if not isinstance(filename, str) or filename != config.filename:
            if filename is None and config.filename == NO_FILE:
                # This is an empty settings file, just return an empty config.
                return Settings(config.filename, config.settings, type=type, big_endian=big_endian)
            raise JSONParseException(f"\"filename\" key in JSON has invalid data \"{filename}\"!", context)

        # Now, parse out the settings in the dict.
        parsedsettings = [Setting.from_json(config.filename, s, [*context, f"settings[{i}]"]) for (i, s) in enumerate(settings)]

        # Finally, go through our config and match up settings to their values.
        for setting in config.settings:
            for parsedsetting in parsedsettings:
                if parsedsetting.name.lower() == setting.name.lower():
                    # It's a match
                    setting.current = parsedsetting.current
                    break
            else:
                raise JSONParseException(f"Setting \"{setting.name}\" could not be found in JSON!", context)

        return Settings(config.filename, config.settings, type=type, big_endian=big_endian)

    def to_json(self) -> Dict[str, Any]:
        jsondata = {
            'filename': self.filename if self.filename != NO_FILE else None,
            'settings': [
                setting.to_json() for setting in self.settings
            ],
        }
        if self.type is not None:
            jsondata['type'] = self.type
        return jsondata

    def to_bytes(self) -> bytes:
        pending: int = 0
        halves: int = 0
        halfname: Optional[str] = None
        location: int = 0
        section: bytes = b''

        for setting in sorted(self.settings, key=lambda setting: setting.order):
            # First, calculate what the default should be in case we need to use it.
            if setting.default is None:
                default = None
            elif isinstance(setting.default, int):
                default = setting.default
            elif isinstance(setting.default, DefaultConditionGroup):
                # Must evaluate settings to figure out the default for this.
                default = setting.default.evaluate(self.settings)

            # Now, figure out if we should defer to the default over the current value
            # (if it is read-only) or if we should use the current value.
            if isinstance(setting.read_only, ReadOnlyCondition):
                read_only = setting.read_only.evaluate(self.settings)
            elif setting.read_only is True:
                read_only = True
            elif setting.read_only is False:
                read_only = False

            if read_only:
                # If it is read-only, then only take the current value if the default doesn't
                # exist. This lets settings that are selectively read-only get a conditional
                # default if one exists that takes precedence over the current value.
                value = setting.current if default is None else default
            else:
                # If the setting is not read-only, then only take the default if the current
                # value is None.
                value = default if setting.current is None else setting.current

            # Now, write out the setting by updating the EEPROM in the correct location.
            if setting.size == SettingSizeEnum.NIBBLE:
                # First, if we have anything to write, write it.
                if value is not None:
                    if halves == 0:
                        pending = (value & 0xF) << 4
                    else:
                        if len(section) != location:
                            raise Exception("Logic error!")
                        section += struct.pack(self.endian + "B", (value & 0xF) | pending)
                else:
                    raise SettingsSaveException(f"Cannot save setting \"{setting.name}\" with a null value!", self.filename)

                # Now, update our position.
                if halves == 0:
                    halfname = setting.name
                    halves = 1
                else:
                    halfname = None
                    halves = 0
                    location += 1

            elif setting.size == SettingSizeEnum.BYTE:
                # First, make sure we aren't in a pending nibble state.
                if halves != 0:
                    raise SettingsSaveException(
                        f"The setting \"{setting.name}\" follows a lonesome half-byte setting \"{halfname}\". Half-byte settings must always be in pairs!",
                        self.filename
                    )

                if setting.length not in {1, 2, 4}:
                    raise SettingsSaveException(f"Cannot save setting \"{setting.name}\" with unrecognized size {setting.length}!", self.filename)

                if value is not None:
                    if len(section) != location:
                        raise Exception("Logic error!")
                    if setting.length == 1:
                        section += struct.pack(self.endian + "B", value)
                    elif setting.length == 2:
                        section += struct.pack(self.endian + "H", value)
                    elif setting.length == 4:
                        section += struct.pack(self.endian + "I", value)
                else:
                    raise SettingsSaveException(f"Cannot save setting \"{setting.name}\" with a null value!", self.filename)

                # Now, update our position.
                location += setting.length

        if halves != 0:
            raise SettingsSaveException(
                f"The setting \"{halfname}\" is a lonesome half-byte setting. Half-byte settings must always be in pairs!",
                self.filename
            )

        return section

    def __str__(self) -> str:
        return json.dumps(self.to_json(), indent=2)

    def __repr__(self) -> str:
        return str(self)


class SettingsConfig:
    # A class that can manifest a list of settings given a particular
    # file. It is not responsible for parsing any settings. It is only
    # responsible for creating the list of settings given a settings
    # definition file.

    def __init__(self, filename: str, settings: List[Setting]) -> None:
        self.filename = filename
        self.settings = settings

    @staticmethod
    def blank() -> "SettingsConfig":
        # It would be weird to display "NO FILE" to the user when there is
        # an error, but virtually all errors arise from parsing the settings
        # file itself, so if this is blank its unlikely errors will happen.
        return SettingsConfig(NO_FILE, [])

    @staticmethod
    def __escaped(data: str) -> str:
        escaped: bool = False
        output: str = ""

        for c in data:
            if escaped:
                output += f"__ESCAPED__{base64.b64encode(c.encode('utf-8')).decode('ascii')}__"
                escaped = False
            elif c == "\\":
                escaped = True
            else:
                output += c
        return output

    @staticmethod
    def __unescaped(data: str) -> str:
        while "__ESCAPED__" in data:
            before, after = data.split("__ESCAPED__", 1)
            if "__" not in after:
                raise Exception("Logic error, expected second __ to find escaped character!")

            escaped, after = after.split("__", 1)
            data = before + base64.b64decode(escaped.encode('ascii')).decode('utf-8') + after
        return data

    @staticmethod
    def __get_kv(filename: str, name: str, setting: str, length: int) -> Dict[int, str]:
        realsetting = setting.strip()
        if realsetting.startswith("values are "):
            realsetting = realsetting[11:]
        if realsetting.startswith("value is "):
            realsetting = realsetting[9:]

        hex_display = False
        if realsetting.endswith(" in hex"):
            realsetting = realsetting[:-7]
            hex_display = True

        def format_val(val: int) -> str:
            nonlocal hex_display
            nonlocal length

            if not hex_display:
                return str(val)

            strval = hex(val)[2:]
            while len(strval) < length:
                strval = "0" + strval
            return strval

        try:
            if "-" in realsetting:
                if " to " in realsetting:
                    raise SettingsParseException(
                        f"Setting \"{name}\" cannot have a range for valid values that includes a dash! \"{setting}\" should be specified like \"20 to E0\".",
                        filename,
                    )

                k, v = realsetting.split("-", 1)
                k = k.strip().replace(" ", "").replace("\t", "")
                key = int(k, 16)
                value = SettingsConfig.__unescaped(v.strip())

                return {key: value}
            else:
                if " to " in realsetting:
                    low, high = realsetting.split(" to ", 1)
                    low = low.strip()
                    high = high.strip()
                    low = low.strip().replace(" ", "").replace("\t", "")
                    high = high.strip().replace(" ", "").replace("\t", "")

                    retdict: Dict[int, str] = {}
                    for x in range(int(low, 16), int(high, 16) + 1):
                        retdict[x] = format_val(x)
                    return retdict
                else:
                    key = int(realsetting.strip(), 16)
                    value = format_val(key)

                    return {key: value}
        except ValueError:
            raise SettingsParseException(
                f"Failed to parse setting \"{name}\", could not understand value \"{setting}\".",
                filename,
            )

    @staticmethod
    def __get_vals(filename: str, name: str, setting: str) -> Tuple[str, List[int]]:
        try:
            name, rest = setting.split(" is ", 1)
            name = SettingsConfig.__unescaped(name.strip())
            vals: List[int] = []

            for val in rest.split(" or "):
                val = val.strip().replace(" ", "").replace("\t", "")
                vals.append(int(val, 16))

            return name, vals
        except ValueError:
            raise SettingsParseException(
                f"Failed to parse setting \"{name}\", could not understand if condition \"{setting}\".",
                filename,
            )

    @staticmethod
    def from_data(filename: str, data: str) -> "SettingsConfig":
        rawlines = data.splitlines()
        lines: List[str] = []
        settings: List[Setting] = []

        for line in rawlines:
            if not line.strip():
                # Ignore empty lines.
                continue
            if line.strip().startswith("#"):
                # Ignore comments.
                continue

            if ":" not in line:
                # Assume that this is a setting entry.
                if not lines:
                    raise SettingsParseException(f"Missing setting name before size, read-only specifier, defaults or value in \"{line}\". Perhaps you forgot a colon?", filename)

                cur = lines[-1]
                if cur.strip()[-1] == ":":
                    cur = cur + line
                else:
                    cur = cur + "," + line

                lines[-1] = cur
            else:
                # Assume that this is a full setting.
                lines.append(line)

        # Keep track of the order settings were loaded, as this is the order they get parsed in.
        order = 0

        pending_insertions: List[Tuple[Tuple[str, str], Setting]] = []

        # Make sure we respect escaping for any character.
        lines = [SettingsConfig.__escaped(line) for line in lines]
        for line in lines:
            # First, get the name as well as the size and any restrictions.
            name, rest = line.split(":", 1)
            name = SettingsConfig.__unescaped(name.strip())
            rest = rest.strip()

            # Now, figure out what size it should be.
            size = SettingSizeEnum.UNKNOWN
            length = 1
            read_only: Union[bool, ReadOnlyCondition] = False
            values: Dict[int, str] = {}
            default: Optional[Union[int, DefaultConditionGroup]] = None
            ordering: Optional[Tuple[str, str]] = None

            if "," in rest:
                restbits = [r.strip() for r in rest.split(",")]
            else:
                restbits = [rest]

            for bit in restbits:
                if "byte" in bit or "nibble" in bit or "half-byte" in bit:
                    if " " in bit:
                        lenstr, units = bit.split(" ", 1)
                        length = int(lenstr.strip())
                        units = units.strip()
                    else:
                        units = bit.strip()

                    if "nibble" in units or "half-byte" in units:
                        size = SettingSizeEnum.NIBBLE
                    elif "byte" in units:
                        size = SettingSizeEnum.BYTE
                    else:
                        raise SettingsParseException(f"Unrecognized unit \"{units}\" for setting \"{name}\". Perhaps you misspelled \"byte\" or \"half-byte\"?", filename)
                    if size != SettingSizeEnum.BYTE and length != 1:
                        raise SettingsParseException(f"Invalid length \"{length}\" for setting \"{name}\". You should only specify a length for bytes.", filename)

                elif "read-only" in bit:
                    condstr = None
                    if " if " in bit:
                        readonlystr, condstr = bit.split(" if ", 1)
                        negate = True
                    elif " unless " in bit:
                        readonlystr, condstr = bit.split(" unless ", 1)
                        negate = False
                    else:
                        # Its unconditionally read-only.
                        read_only = True
                        readonlystr = bit

                    if readonlystr.strip() != "read-only":
                        raise SettingsParseException(f"Cannot parse read-only condition \"{bit}\" for setting \"{name}\"!", filename)
                    if condstr is not None:
                        condname, condvalues = SettingsConfig.__get_vals(filename, name, condstr)
                        read_only = ReadOnlyCondition(filename, name, condname, condvalues, negate)

                elif "default" in bit:
                    if " is " in bit:
                        defstr, rest = bit.split(" is ", 1)
                        if defstr.strip() != "default":
                            raise SettingsParseException(f"Cannot parse default \"{bit}\" for setting \"{name}\"!", filename)

                        condstr = None
                        if " if " in rest:
                            rest, condstr = rest.split(" if ", 1)
                            negate = False
                        elif " unless " in rest:
                            rest, condstr = rest.split(" unless ", 1)
                            negate = True
                        else:
                            # Its unconditionally a default.
                            pass

                        # Now, see if the value is computed from another setting.
                        rest = rest.strip()
                        if "value of " in rest:
                            valname = rest[9:].strip()
                            adjust = 0

                            if "+" in valname:
                                newname, add = valname.rsplit('+', 1)
                                newname = newname.strip()
                                add = add.strip()

                                try:
                                    adjust = int(add, 16)
                                    valname = newname
                                except ValueError:
                                    # Wasn't a correct adjustment, assume its the name.
                                    pass

                            if "-" in valname:
                                newname, add = valname.rsplit('-', 1)
                                newname = newname.strip()
                                add = add.strip()

                                try:
                                    adjust = -int(add, 16)
                                    valname = newname
                                except ValueError:
                                    # Wasn't a correct adjustment, assume its the name.
                                    pass

                            valname = SettingsConfig.__unescaped(valname)

                            if default is None:
                                default = DefaultConditionGroup(filename, name, [])
                            if not isinstance(default, DefaultConditionGroup):
                                raise SettingsParseException(f"Cannot specify an unconditional default alongside conditional defaults for setting \"{name}\"!", filename)

                            def getmax(size: SettingSizeEnum, length: int) -> int:
                                if size == SettingSizeEnum.NIBBLE:
                                    return 0x10
                                elif size == SettingSizeEnum.BYTE:
                                    if length == 1:
                                        return 0x100
                                    elif length == 2:
                                        return 0x10000
                                    elif length == 4:
                                        return 0x100000000

                                raise SettingsParseException(
                                    f"Cannot determine default \"{bit}\" values for setting \"{name}\"!",
                                    filename,
                                )

                            if condstr is None:
                                # This is just a series of "default is <X + adjust> if <valname> is <X>" internally.
                                maxval = getmax(size, length)
                                for x in range(maxval):
                                    default.conditions.append(DefaultCondition(valname, [x], False, (x + adjust) % maxval))
                            else:
                                condname, condvalues = SettingsConfig.__get_vals(filename, name, condstr)

                                if condname != valname:
                                    # This is a crazy complicated predicate where we would need to check if one setting is a certain
                                    # value and if so scoop up the value of another setting. Hopefully we don't need to support this.
                                    raise SettingsParseException(
                                        f"Cannot handle extracting value from \"{valname}\" while being dependent on \"{condname}\" for setting \"{name}\"!",
                                        filename,
                                    )

                                # This is just the same series as above, but with a specific set of X instead of a full range.
                                maxval = getmax(size, length)
                                for x in condvalues:
                                    default.conditions.append(DefaultCondition(valname, [x], False, (x + adjust) % maxval))
                        else:
                            rest = rest.strip().replace(" ", "").replace("\t", "")
                            defbytes = bytes([int(rest[i:(i + 2)], 16) for i in range(0, len(rest), 2)])
                            if size != SettingSizeEnum.UNKNOWN and len(defbytes) == 1:
                                defaultint = defbytes[0]
                            else:
                                if size == SettingSizeEnum.NIBBLE:
                                    defaultint = struct.unpack("<B", defbytes[0:1])[0]
                                elif size == SettingSizeEnum.BYTE:
                                    if length == 1:
                                        defaultint = struct.unpack("<B", defbytes[0:1])[0]
                                    elif length == 2:
                                        defaultint = struct.unpack("<H", defbytes[0:2])[0]
                                    elif length == 4:
                                        defaultint = struct.unpack("<I", defbytes[0:4])[0]
                                    else:
                                        raise SettingsParseException(
                                            f"Cannot convert default \"{bit}\" for setting \"{name}\" because we don't know how to handle length \"{length}\"!",
                                            filename,
                                        )
                                else:
                                    raise SettingsParseException(f"Must place default \"{bit}\" after size specifier in setting \"{name}\"!", filename)

                            if condstr is None:
                                if default is not None:
                                    if isinstance(default, DefaultConditionGroup):
                                        raise SettingsParseException(f"Cannot specify an unconditional default alongside conditional defaults for setting \"{name}\"!", filename)
                                    else:
                                        raise SettingsParseException(f"Cannot specify more than one default for setting \"{name}\"!", filename)
                                default = defaultint
                            else:
                                if default is None:
                                    default = DefaultConditionGroup(filename, name, [])
                                if not isinstance(default, DefaultConditionGroup):
                                    raise SettingsParseException(f"Cannot specify an unconditional default alongside conditional defaults for setting \"{name}\"!", filename)

                                condname, condvalues = SettingsConfig.__get_vals(filename, name, condstr)
                                default.conditions.append(DefaultCondition(condname, condvalues, negate, defaultint))
                    else:
                        raise SettingsParseException(f"Cannot parse default for setting \"{name}\"! Specify defaults like \"default is 0\".", filename)

                elif "display" in bit:
                    # This is a directive for controlling where the setting is displayed.
                    if " before " in bit:
                        display, settingname = bit.split(" before ", 1)
                        display = display.strip()
                        settingname = SettingsConfig.__unescaped(settingname.strip())
                        directive = "before"
                    elif " after " in bit:
                        display, settingname = bit.split(" after ", 1)
                        display = display.strip()
                        settingname = SettingsConfig.__unescaped(settingname.strip())
                        directive = "after"
                    else:
                        raise SettingsParseException(f"Couldn't understand position \"{bit}\" for setting \"{name}\". Perhaps you misspelled \"before\" or \"after\"?", filename)

                    if display != "display":
                        raise SettingsParseException(f"Couldn't understand position \"{bit}\" for setting \"{name}\".", filename)

                    ordering = (directive, settingname)

                else:
                    # Assume this is a setting value.
                    values.update(SettingsConfig.__get_kv(filename, name, bit, 1 if size == SettingSizeEnum.NIBBLE else (length * 2)))

            if size == SettingSizeEnum.UNKNOWN:
                raise SettingsParseException(f"Setting \"{name}\" is missing a size specifier!", filename)
            if read_only is not True and not values:
                raise SettingsParseException(f"Setting \"{name}\" is missing any valid values and isn't read-only!", filename)

            # Add it to either the current list of settings, or the queue of settings to insert when we're done.
            new_setting = Setting(
                name,
                order,
                size,
                length,
                read_only,
                values,
                default=default,
            )
            if ordering is None:
                settings.append(new_setting)
            else:
                pending_insertions.append((ordering, new_setting))

            # Keep track of order
            order += 1

        # Now, insert pending settings where they belong.
        while pending_insertions:
            # Search all pending insertions to see if we can add one of them.
            # Its okay for pending insertions to depend on the placement of other
            # pending insertions as long as one of them is placeable each time.
            for i in range(len(pending_insertions)):
                (directive, settingname), needs_placement = pending_insertions[i]
                location: Optional[int] = None

                # Look for a potential insertion spot.
                for j, setting in enumerate(settings):
                    if setting.name == settingname:
                        if directive == "before":
                            location = j
                        elif directive == "after":
                            location = j + 1
                        else:
                            raise Exception("Logic error!")
                        break

                if location is not None:
                    # Get rid of the pending setting, we're about to add it to the master list.
                    del pending_insertions[i]
                    settings = [*settings[:location], needs_placement, *settings[location:]]
                    break
            else:
                allsettings = ", ".join([f'"{s.name}"' for _, s in pending_insertions])
                raise SettingsParseException(
                    f"We couldn't figure out where to place the following settings: {allsettings}. Did you accidentially create a display order loop?",
                    filename,
                )

        # Finally, verify that nibbles come in pairs, which was dependent on the original
        # file-ordering, not in the dependent ordering we calculated here.
        halves = 0
        halfname: Optional[str] = None
        for setting in sorted(settings, key=lambda setting: setting.order):
            if setting.size == SettingSizeEnum.NIBBLE:
                halves = 1 - halves
                halfname = None if halves == 0 else setting.name
            elif setting.size == SettingSizeEnum.BYTE:
                if halves != 0:
                    raise SettingsParseException(
                        f"The setting \"{setting.name}\" follows a lonesome half-byte setting {halfname}. Half-byte settings must always be in pairs!",
                        filename
                    )
        if halves != 0:
            raise SettingsSaveException(
                f"The setting \"{halfname}\" is a lonesome half-byte setting. Half-byte settings must always be in pairs!",
                filename
            )

        return SettingsConfig(filename, settings)

    @property
    def defaults(self) -> bytes:
        pending = 0
        halves = 0
        halfname: Optional[str] = None
        defaults: List[bytes] = []

        for setting in sorted(self.settings, key=lambda setting: setting.order):
            if setting.default is None:
                default = 0
            elif isinstance(setting.default, int):
                default = setting.default
            elif isinstance(setting.default, DefaultConditionGroup):
                # Must evaluate settings to figure out the default for this.
                default = setting.default.evaluate(self.settings)

            if setting.size == SettingSizeEnum.NIBBLE:
                if halves == 0:
                    pending = (default & 0xF) << 4
                else:
                    defaults.append(bytes([(default & 0xF) | pending]))

                if halves == 0:
                    halfname = setting.name
                    halves = 1
                else:
                    halfname = None
                    halves = 0
            elif setting.size == SettingSizeEnum.BYTE:
                if halves != 0:
                    raise SettingsSaveException(
                        f"The setting \"{setting.name}\" follows a lonesome half-byte setting \"{halfname}\". Half-byte settings must always be in pairs!",
                        self.filename
                    )
                if setting.length == 1:
                    defaults.append(struct.pack("<B", default))
                elif setting.length == 2:
                    defaults.append(struct.pack("<H", default))
                elif setting.length == 4:
                    defaults.append(struct.pack("<I", default))
                else:
                    raise SettingsSaveException(f"Cannot save setting \"{setting.name}\" with unrecognized size {setting.length}!", self.filename)

        if halves != 0:
            raise SettingsSaveException(
                f"The setting \"{halfname}\" is a lonesome half-byte setting. Half-byte settings must always be in pairs!",
                self.filename
            )

        return b"".join(defaults)
