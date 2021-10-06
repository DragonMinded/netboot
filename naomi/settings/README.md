# naomi.settings

Collection of routines written in Python for safe manipulation of 128-byte
Naomi EEPROM files using supplied system definition files. Essentially, given
a valid 128-byte EEPROM or a valid 4-byte Naomi ROM serial and a set of system
and game definition files, `naomi.settings` will provide you a high-level
representation of valid settings including their defaults, valid values and
relationships to each other. Settings editors can be built using this module
which work together with `naomi.NaomiEEPRom` and `naomi.NaomiSettingsPatcher`
to make the settings available when netbooting a game on a Naomi system.

## Setting

A single setting, with its name, default, current value, possible allowed values,
and any possible relationship to other settings. Note that any relationship,
if it exists, will only be to other Setting objects inside a `Settings` class.
Note that you should not attempt to construct an instance of this yourself.
You should only work with previously-constructed instances of it as found inside
an instance of `Settings`.

### name property

The name of this setting, as a string. This is what you should display to a user
if you are developing a settings editor.

### order property

The order that this setting showed up in the definition file that created it.
Note that if you are implementing an editor, you can safely ignore this as the
settings will already be placed in the correct display order.

### size property

The size of this setting, as an instance of SettingSizeEnum. The valid values
for this are `SettingSizeEnum.NIBBLE` and `SettingSizeEnum.BYTE`. Note that if
you are developing an editor, you can safely ignore this as the `values` property
will include all valid values that this setting can be set to. You do not have to
understand or manipulate this in any way and it is only present so that other
parts of the `naomi.settings` module can do their job properly.

### length property

The length in bytes this setting takes up, if the `size` property is `SettingSizeEnum.BYTE`.
If the `size` property is instead `SettingSizeEnum.NIBBLE` then this will always
be set to 1. Note that much like the `size` property if you are implementing an
editor you can safely ignore this property for the same rationale as above.

### read_only property

Whether this property is read-only or not. Some settings are not modifiable, such
as the system serial number. Other settings are only modifiable if other settings
are set to some value, such as the "Continue" setting on Marvel vs. Capcom 2 which
is dependent on "Event" mode being off. If this property is "False" then this setting
is user-editable under all circumstances. If this property is "True" then this setting
is never user-editable. If this property is an instance of `ReadOnlyCondition` then
it depends on some other settings for whether it is read-only. You can call the
`evaluate()` method on the instance of `ReadOnlyCondition` which takes a list of
`Setting` objects (this setting's siblings as found in a `Settings` object) and returns
a boolean. If that boolean is "True", then this setting is currently read-only because
of some other setting's value. If the boolean is "False", then the setting is currently
editable because of some other setting's value.

In the Naomi Test Mode, settings that are always read-only are hidden completely from
the user. Settings which are never read-only are displayed to the user. And settings
which are conditionally read-only will be conditionally hidden based on whether they
are read-only. It is recommended that your editor perform a similar thing when you
display settings. Settings whose `read_only` property is "False" should always be
displayed. Settings whose `read_only` property is "True" should be completely hidden
from the user. Settings whose `read_only` property is a `ReadOnlyCondition` should be
evaluated and then the setting either grayed out when it is "True" or conditionally
hidden from the user.

### values property

A dictionary whose keys are integers which the `current` property could be set
to, and whose values are the strings which should be displayed to the user for
those value selections. Note that if a setting is always read-only this may instead
be None. It is guaranteed to be a dictionary with at least one value whenever a
setting is user-editable.

### current property

The current integer value that the setting is set to. In order to display the correct
thing to a user, you should use this as a key into the `values` property to look up
the correct string to display.

### default property

The default value for this setting. Note that under some circumstances, this may
not be available and will return None. You can safely ignore this property if you are
developing an editor. If you wish to provide a "defaults" button in your editor, it
is recommended to instead use the `from_serial()` or `from_rom()` method on an instance of
`SettingsManager` which will return you a new `SettingsWrapper` with default values.
This will correctly handle system and game defaults as well as dependendent default
settings.

## Settings

A class which represents a collection of settings that can be used to manipulate
a section of an EEPROM file. Note that you should not attempt to construct
this yourself. You should only work with previously-constructed instances of
it as found inside an instance of `SettingsWrapper`.

### filename property

The name of the settings definition file that was used to create this collection.
Note that this is not a fully qualified path, but instead just the name of
the file, like "system.settings" or "BBG0.settings". If you wish to look up
the actual file location given this property, use the `files` property on an
instance of `SettingsManager`.

### type property

An instance of SettingType which specifies whether this collection of settings
is a system settings section or a game settings section in an EEPROM. Valid
values are `SettingType.SYSTEM` and `SettingType.GAME`.

### settings property

A python list of `Setting` objects, representing the list of settings that
can be mofidied or displayed. You should not assign to this property directly
when modifying settings in a settings editor you are implementing. However,
you are welcome to modify the properties of each setting in this list directly.

### length property

An integer representing how many bytes long the section of EEPROM represented
by this collection is. For system settings, this will always be 16 since the
system section is hardcoded at 16 bytes. For game settings, this will be
determined by the settings definition file that was looked up for the game
in question.

## SettingsWrapper

A class whose sole purpose is to encapsulate a group of system settings,
game settings and the serial number of the game that the system and game
settings go with. This is returned by many methods in `SettingsManager`
and taken as a parameter of several more methods in `SettingsManager.

Note that you should not attempt to construct this yourself. You should
only work with previously-constructed instances of it as returned by
methods in `SettingsManager`.

### serial property

The 4-byte serial of the game this `SettingsWrapper` instance has been
created for.

### system

A collection of settings that manipulate the system section of the EEPROM
for the game this instance has been created for. This is inside of a
`Settings` wrapper object.

### game

A collection of settings that manipulate the game section of the EEPROM
for the game this instance has been created for. This is inside of a
`Settings` wrapper object.

### to_json() method

Converts the current instance of `SettingsWrapper` to a dictionary suitable
for passing to `json.dumps`. This is provided as a convenience wrapper so
that if you are implementing a web interface you don't have to serialize
anything yourself. To unserialize a dictionary that you get from this method,
call the `from_json` method on an instance of `SettingsManager`.

## SettingsManager

The `SettingsManager` class manages the ability to parse a 128-byte EEPROM
file given a directory of settings definitions. It is responsible for
identifying the correct files for patching given an EEPROM or ROM serial.
It is also responsible for taking a modified list of settings and writing
a new EEPROM file.

Note that default definitions are included with this module. To grab the
default definitions directory, use the `get_default_settings_directory` function
which will return a fully qualified path to the settings directory of this
module.

Note that since this is parsing user-supplied settings definitions files,
there can be errors in processing those files. In any function that returns
a `SettingsWrapper` instance, a `SettingsParseException` can be thrown.
This is a subclass of `Exception` so you can get the error message to
display to a user by calling `str()` on the exception instance. The instance
will also have a `filename` property which is the filename of the settings
definition file that caused the problem.

There can also be problems in saving EEPROM settings given the same definitions
files. In this case, a `SettingsSaveException` can be thrown. This is identical
to `SettingsParseException` save for the source, so all of the above documentation
applies.

There can also be problems in deserializing JSON data when calling the
`from_json()` method. In this case, a `JSONParseException` can be thrown. Similar
to the above two exceptions, calling `str()` on the instance will give you back
an error message that can be displayed to a user. The instance will also have
a `context` property which is the exact location in the JSON where the failure
occured as represented by a list of attributes that were dereferenced in the
JSON to get to the section that had an error.

### Default Constructor

Takes a single string argument "directory" which points at the directory
which contains settings definition files and returns an instance of the
`SettingsManager` class. In this repository, that directory is
`naomi/settings/definitions/`. Note that the settings definitions in this
repository can be found by using the `get_default_settings_directory` function.
An example of how to initialize this is as follows:

```
from naomi.settings import get_default_settings_directory, SettingsManager

dir = get_default_settings_directory()
man =  SettingsManager(dir)
```

### files property

An instance of `SettingsManager` has the "files" property, which returns
a dictionary of recognized settings definitions in the directory supplied to
the default constructor. The returned dictionary has keys representing the
settings definition file, such as "system.settings" or "BBG0.settings". The
values of the dictionary are fully qualified system paths to the file in
question.

### from_serial() method

Takes a single bytes argument "serial" as retrieved from Naomi ROM header
and uses that to construct a `SettingsWrapper` class representing the
available settings for a game that has the serial number provided. This
can be used when you want to edit settings for a game but do not have an
EEPROM already created. This will read the definitions files and create
a `SettingsWrapper` with default settings. This can be then passed to the
`to_eeprom()` function to return a valid 128-byte EEPROM representing the
default settings.

### from_rom() method

Takes a NaomiRom instance argument "rom" and a NaomiRomReginEnum argument
"region" and retrieves any requested system defaults from the Naomi ROM
header. It uses that as well as the game's settings definition file to create
a default EEPROM that is then used to construct a `SettingsWrapper` class
repressenting the default settings as a Naomi would create them on first
boot. This can then be edited or passed to the `to_eeprom()` function to
return a valid 128-byte EEPROM representing the edited settings.

### from_eeprom() method

Takes a single bytes argument "data" as loaded from a valid 128-byte
EEPROM file or as grabbed from the `data` property of an instance of
`NaomiEEPRom` and constructs a `SettingsWrapper` class representing the
available settings for a game that matches the serial number provided in
the EEPROM file. This can be used when you want to edit the settings for
a game and you already have the EEPROM file created. This will read the
definitions file and parse out the current settings in the EEPROM and
return a `SettingsWrapper` with those settings. This can then be modified
and passed to the `to_eeprom()` function to return a valid 128-byte EEPROM
representing the current settings.

### from_json() method

Takes a single dictionary argument "jsondict" and deserializes it to
a `SettingsWrapper` instance. The dictionary argument can be retrieved
by calling the `to_json()` method on an existing `SettingsWrapper` instance.
This is provided specifically as a convenience method for code wishing to
provide web editor interfaces. A recommended workflow is to create an
instance of `SettingsManager`, request a `SettingsWrapper` by calling
either `from_eeprom()` or `from_serial()` as appropriate, calling `to_json()`
on the resulting `SettingsWrapper` class and then passing that to
`json.dumps` to get valid JSON that can be sent to a JS frontend app. After
the frontend app has manipulated the settings by modifying the current
value of each setting, you can use `json.loads` to get back a dictionary
that can be passed to this function to get a deserialized `SettingsWrapper`
class. The deserialized `SettingsWrapper` instance can then be passed to
the `to_eeprom()` function to return a valid 128-byte EEPROM representing
the settings chosen by the JS frontend.

### to_eeprom() method

Given an instance of `SettingsWrapper` returned by either `from_serial()`,
`from_eeprom()` or `from_json()`, calculates and returns a valid 128-byte
EEPROM file that represents the settings. Use this when you are finished
modifying system and game settings using code and wish to generate a valid
EEPROM file that can be modified with `NaomiEEPRom`, placed in an emulator's
data directory to load those settings or attached to a Naomi ROM using the
`naomi.NaomiSettingsPatcher` class so that the settings are written when
netbooting the rom on a Naomi system.
