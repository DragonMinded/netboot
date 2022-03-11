# naomi

Collection of routines written in Python for manipulating Naomi ROM and EEPROM
files. This is geared towards enthusiasts building their own netboot servers or
RPI setups and provides libraries for examining ROM and manipulating ROM headers
as well as attaching EEPROM settings to ROM files and manipulating their contents.
It is fully typed and requires a minimum of Python 3.6 to operate.

## NaomiEEPRom

The NaomiEEPRom class provides high-level accessors to a 128-byte Naomi EEPROM
dump as obtained through a ROM dumper or from an emulator's saved state. It handles
correcting various CRCs as well as allowing high-level access to the duplicated
game and system settings sections. Use this to create or manipulate a raw EEPROM
data file.

### Default Constructor

Takes a single byte argument "data" and verifies that it is a valid 128-byte EEPROM
before returning an instance of the `NaomiEEPRom` class used to manipulate that
data. If any of the CRCs do not match this will raise a `NaomiEEPRomException`.

### NaomiEEPRom.default

An alternate constructor that takes a single byte argument "serial" and an optional
byte argument "game_defaults" and creates a valid EEPROM based on this data, returning
an instance of the `NaomiEEPRom` class that can be used to manipulate this newly
created EEPROM. The serial argument should be exactly bytes and begin with a "B",
followed by two characters and finally a digit, as represented by a bytestring. This
is a Naomi system restriction. Optionally, a string of bytes can be given in the
"game_defaults" section which will be used to determine the length and default
values of the game section of the EEPROM.

### NaomiEEPRom.validate

A static method that takes a byte argument "data" and checks it for validity. This
includes making sure the length is 128 bytes and that all CRCs are correct. Optionally
you can pass in the boolean keyword argument "only_system" set to True to only check
that the system section is valid. This is useful for validating EEPROMs where the BIOS
has written system settings but the game has not yet booted and created its own
defaults yet. You can use this function to ensure that passing data to the default
constructor will not result in an exception.

### data property

An instance of NaomiEEPRom has the "data" property, which returns a bytes object
representing the current 128-byte EEPROM. This will have all CRC sections fixed.
Use the "data" property to retrieve the EEPROM for writing to a file or sending to
a Naomi system after manipulating data using the NaomiEEPRom class. Note that this
is read-only, you should not attempt to manipulate the raw data using this property.

## serial property

Returns the 4 byte serial that is found in the system section of the EEPROM. This
will match a serial given in the `NaomiEEPRom.default` constructor when it is used.
Use this to help determine what game an EEPROM goes with. Note that this is read-only.
To modify the serial, create a new EEPROM with that serial. Game settings and system
settings are not compatible across games on the Naomi platform.

## length property

The length in bytes as an integer of the game section of the EEPROM. If the game section
is not valid this return 0 bytes. Otherwise it returns the length of the game section
itself. This property is writeable. If you provide it a new value, the game section
will be resized to that length. Use this to determine the bounds of the `game` section
as documented below, as well as to resize the `game` section.

## system property

Returns a bytes-like wrapper object representing the system section of the EEPROM.
This operates like a bytearray object in Python. That means you can access or mutate
any byte or section in the system area using this property. Note that this wrapper
object takes care of reading from and writing to both mirrors of the system section in
the EEPROM file as well as ensuring that the CRC is correct. Note also that the system
section is hard-coded to 16 bytes in length which cannot be modified. This is a system
restriction on the Naomi platform. Much like bytes objects in python, accessing a single
byte returns an integer in the range of 0-255, but accessing a range returns a bytes
object.

A simple example of reading bytes 6-8 of the system section:

```
eeprom = NaomiEEPRom(somedata)
print(eeprom.system[6:8])  # Will print a bytes object of length 2.
```

A simple example of writing bytes 10-12 of the system section:

```
eeprom = NaomiEEPRom(somedata)
eeprom.system[10:12] = b"\x12\x34"
```

## game property

Returns a bytes-like wrapper object representing the game section of the EEPROM. This
operates identically to the `system` property as documented above, only it accesses the
game section of the EEPROM. Note that for this to work properly, the game section needs
to be initialized by setting the `length` property on the instance of `NaomiEEPRom`. If
you are manipulating an existing EEPROM file, this property will already be set for you.

Note that this wrapper object includes a `valid` property which returns if the current
section is valid in the EEPROM you are manipulating. This will always be `True` for
the system section. However, if you access the game section on a newly-created EEPROM
without setting defaults or a length, the game property's `valid` property will return
`False`.

An example of verifying that the game section is valid:

```
eeprom = NaomiEEPRom.default(serial=b"BBG0")
print(eeprom.game.valid)  # Will print "False" as the EEPROM was created without a game section default.
eeprom.length = 20
print(eeprom.game.valid)  # Will print "True" as the EEPROM game section was initialized to be 20 bytes long.
```

## NaomiRom

The NaomiRom class provides high-level accessors to a Naomi ROM header as found
at the beginning of a ROM file suitable for netbooting. It handles decoding all
sections of the ROM header as well as allowing modification and even creation of
new ROM header sections given valid data. Use this if you wish to manipulate or
create your own Naomi ROM files form scratch.

### Default Constructor

Takes a single byte argument "data" and uses it as the ROM image where the header
will be extracted. Note that there is no CRC over the ROM header so any data that
is 1280 bytes or longer will appear valid.

### NaomiRom.default

An alternate constructor that creates an entirely blank Naomi ROM containing no
loaded executable or test sections and no ROM name. Use this when you want to
programatically construct a ROM image, such as when you are building a final ROM
in a homebrew program you are building for the Naomi platform.

### valid property

An instance of NaomiRom has the "valid" property which will be "True" when the ROM
passed into the constructor is a valid Naomi ROM and "False" otherwise. This is a
read-only property as the vailidity of a ROM is entirely dictated by the data
passed into the constructor.

### data property

The ROM data, as passed into the constructor for the instance of NaomiRom, or as
created when using `NaomiRom.default` alternate constructor. Note that when any
of the following properties are written, the `data` property will be changed to
reflect those settings. Use this to retrieve the updated ROM after you've made
adjustments to the values you wish to change.

### publisher property

The publisher of this ROM, as a string. When read, grabs the current publisher
of the ROM image. When written, updates the publisher to the new string provided.

### names property

A dictionary of names indexed by region. Given the current system region, the names
that show up here will also be the names that show up in the test menu for a given
game. Note that there are the following constants that can be used to index into the
names list: `NaomiRomRegionEnum.REGION_JAPAN`, `NaomiRomRegionEnum.REGION_USA`,
`NaomiRomRegionEnum.REGION_EXPORT`, `NaomiRomRegionEnum.REGION_KOREA`, and finally
`NaomiRomRegionEnum.REGION_AUSTRALIA`. Note that the last region, Australia, exists
in many ROM files but is not accessible as there is no Australia BIOS for the Naomi
platform. When read, grabs a dictionary of names of the ROM given the region. When
written, updates the ROM names by region using the dictionary provided.

### sequencetexts property

A list of 8 sequence texts that are used by the game for coin insertion messages.
Many ROMs only have the first sequence set. When read, grabs all 8 sequence texts
and returns a list of them. When written, updates the sequence texts to the new
list of strings provided.

### defaults property

A dictionary of NaomiEEPROMDefaults instance representing what defaults the BIOS will
set in the system EEPROM section when initializing the EEPROM on first boot. Note
that this is indexed by the same enumeration as the "names" property. When read, grabs
the defaults and returns them. When written, extracts values from the provided
NaomiEEPROMDefaults instances and updates the per-region defaults in the ROM accordingly.

### date property

A `datetime.date` instance representing what date the ROM was build and released.
When read, returns the current date in the ROM header. When written, updates the
date of the ROM with the new `datetime.date` provided.

### serial property

A 4-byte bytestring representing the serial number of the ROM. This is used to tie
EEPROM data to the ROM itself and lets the Naomi know when to reset certain defaults.
When read, returns the current serial from the ROM header. When written, updates the
serial in the ROM header.

### regions property

A list of NaomiRomRegionEnum values representing valid regions this ROM will run under.
Uses the same region constants as the `names` property. When read, returns a list of
the valid regions this ROM executes under. When written, updates the list of regions
the ROM is allowed to execute under. When booting, the Naomi BIOS will check the
current region against this list and show an error if the current region is not
included in the list.

### players property

A list of integers representing the valid number of player configurations that this
ROM will boot under. Valid player numbers include 1, 2, 3 and 4. When read, returns
a list of all valid number of player configurations that this game will boot with.
When written, updates the list of player configurations. When booting, the Naomi
BIOS will check the "Number of Players" setting in the system assignments and see
if that setting appears in this list.

### frequencies property

A list of frequencies that the monitor is allowed to run at for this ROM. This
includes the values 15 and 31. On read, returns the list of allowed frequencies.
On write, updates the list of allowed frequencies. On boot, the Naomi BIOS will
check the current horizontal refresh rate of the system as controlled by a DIP
switch and show an error if it isn't in the list of allowed frequencies.

### orientations property

A list of strings representing the allowed orientations for the monitor for this
ROM. The includes the values "horizontal" and "vertical". On read, returns the
list of all acceptable orientations. On write, updates that list based on the
provided list of strings. On boot, the Naomi BIOS will check the current "Monitor
Orientation" setting in the system assignments and see if that orientation is
on this list.

### servicetype property

A string value of either "individual" or "common" for the expected service button
type for the ROM. On read, returns either "individual" or "common" to represent
the current service type selected. On write, updates the service type to match
the string provided.

### main_executable property

An instance of a NaomiExecutable including sections of the ROM that the Naomi
BIOS will copy before executing the ROM, as well as the entrypoint in main RAM
that the BIOS will jump to after copying sections. On read, returns the current
list of sections to copy as well as the main entrypoint, as encapsulated as an
instance of NaomiExecutable. On write, it updates the ROM to the new executable
configuration by unpacking the NaomiExecutable instance given.

### test_executable property

This property is identical to the `main_executable` property, except for it
represents the code and entrypoint that the Naomi BIOS will use when executing
the "Game Test Mode" section of the test menu. It can be similarly read and written.

## NaomiSettingsPatcher

The NaomiSettingsPatcher class provides logic for attaching an EEPROM or SRAM configuration
file to a Naomi ROM so that it can be written to the EEPROM/SRAM when netbooting that
ROM. Note that this is not a supported feature of the Naomi platform, so it uses
an executable stub that it attaches to the ROM in order to make this work. If you
do not care what executable stub is attached and only want to patch settings into
a ROM file, use the `get_default_trojan` function which will return a bytes
object suitable for passing into a `NaomiSettingsPatcher` constructor.

### Default Constructor

Takes a bytes "rom" argument and a bytes "trojan" argument creates an instance of
NaomiSettingsPatcher which can attach or retrieve previously-attached EEPROM or SRAM settings
in a Naomi ROM file suitable for netbooting. An example of how to initialize this
is as follows:

```
from naomi import NaomiSettingsPatcher, get_default_trojan

patcher = NaomiSettingsPatcher(somedata, get_default_trojan())
```

### data property

The same bytes as passed to the `NaomiSettingsPatcher` constructor. After calling
`put_settings()` as documented below, this will be updated to the new ROM contents
with the settings applied. A recommended workflow is to patch ROMs on-the-fly when
netbooting by creating an instance of `NaomiSettingsPatcher` with the ROM data you
were about to send, calling `put_settings()` with the settings you wish to attach,
and then getting the data using this property and sending it down the wire to the
Naomi system. Note that you can attach either an EEPROM file (128 bytes) or an SRAM
file (32kb) but not both.

### serial property

An instance of NaomiSettingsPatcher has the `serial` property. When read, this
will examine the serial of the Naomi ROM passed into the constructor and return the
4 byte serial number, suitable for matching against an EEPROM's system serial. Note
that this property is read-only.

### rom property

Returns a `NaomiRom` instance that encapsulates the ROM passed into the patcher. This
instance should not be edited, as it will not be read again when performing the patches.
Note that this property is read-only.

### has_eeprom property

Returns `True` if the ROM passed into the patcher has an attached EEPROM file. Returns
`False` otherwise.

### eeprom_info property

Returns an optional instance of NaomiSettingsInfo if the ROM has a configured EEPROM
section. If the ROM does not have a configured EEPROM section, this returns `None`.
The NaomiSettingsInfo instance represents the configuration passed to `put_eeprom()`
on a previous invocation. Note that this property is read-only.

### get_eeprom() method

Returns a 128-byte EEPROM bytestring that was previously attached to the Naomi ROM,
or `None` if this ROM does not include any EEPROM settings.

### put_eeprom() method

given a bytes "eeprom" argument which is a valid 128-byte EEPROM, ensures that it
is attached to the Naomi ROM such that the settings are written when netbooting the
ROM image. If there are already EEPROM settings attached to the ROM, this overwrites
those with new settings. If there are not already settings attached, this does the
work necessary to attach the settings file as well as the writing trojan supplied to
the `NaomiSettingsPatcher` constructor.

Valid EEPROM files can be obtained form a number of places. If you use an emulator
to set up system and game settings, then the EEPROM file that emulator writes can
usually be supplied here to make your game boot to the same settings. If you use
the `NaomiEEPRom` class to manipulate an EEPROM, the data it produces can also be
supplied here to force the Naomi to use the same settings.

Optionally, pass in the boolean keyword argument "enable_sentinel" set to True and
the Naomi ROM will re-initialize the settings when netbooting even if the last game
netbooted was this game. Use this when iterating over settings that you want to choose
so that you can ensure the settings are written. If you do not provide this argument,
the default behavior is that settings will not be overwritten when we netboot a game
that is already running on the system.

Optionally, pass in the boolean keyword argument "enable_debugging" set to True
which forces the Naomi to display debugging information on the screen before booting
the game. Use this to see what is actually going on under the hood when using the
settings patching feature.

Optionally, pass in the boolean keyword argument "verbose" set to True which forces
the `put_eeprom()` function to output progress text to stdout. Use this if you are
making a command-line tool and wish to display information about the patch process
to the user.

### has_sram property

Returns `True` if the ROM passed into the patcher has an attached SRAM file. Returns
`False` otherwise.

### get_sram() method

Returns a 32k-byte SRAM bytestring that was previously attached to the Naomi ROM, or
`None` if this ROM does not include any SRAM settings.

### put_sram() method

given a bytes "settings" argument which is a valid 32k-byte SRAM, ensures that it is
attached to the Naomi ROM such that the settings are written when netbooting the ROM
image. If there are already SRAM settings attached to the ROM, this overwrites those
with new settings. If there are not already settings attached, this does the work
necessary to attach the settings file.

Valid SRAM files can be obtained from an emulator that is capable of writing an SRAM
file. This only makes sense to use in the context of atomiswave conversions and in
a select few Naomi games that store their settings in SRAM such as Ikaruga.

Optionally, pass in the boolean keyword argument "verbose" set to True which forces
the `put_settings()` function to output progress text to stdout. Use this if you are
making a command-line tool and wish to display information about the patch process
to the user.
