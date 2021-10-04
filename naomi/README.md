# naomi

Collection of utilities written in Python for manipulating Naomi ROM and EEPROM
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
