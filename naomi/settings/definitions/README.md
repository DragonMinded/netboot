# Settings Definitions Format

Settings definition files are meant to be simple, human readable documentation
for a game's EEPROM settings. They are written in such a way that on top of
being human-readable documentation, they can also be parsed by
`naomi.settings.SettingsManager` to help with making settings editors for any
game on the Naomi platform. Each setting in a settings definition file represents
how to parse some number of bytes in a game's EEPROM. You'll notice that while
there is a size specifier for each setting there is no location specifier. That's
because each setting is assumed to come directly after the previous setting in
the section.

All settings sections in an game's EEPROM are assumed to be little-endian, much
like the Naomi system itself. Defaults and valid values are specified as hex
digits as copied directly out of a hex editor. When specifying half-byte settings,
the first setting is assumed to be the top half of the byte (the first hex digit
that appears when reading the EEPROM in a hex editor) and the second setting is
assumed to be the bottom half of the byte. All half-byte settings are expected
to come in pairs.

Aside from the "system.settings" file, all settings files are named after the
serial number of the game they are associated with. The serial number for the
game can be found by looking at the ROM header using a tool such as `rominfo`,
or by looking at bytes 3-7 of an EEPROM that you got out of an emulator and
loaded into a hex editor.

The only necessary parts of a setting are the name and the size. If the setting
is user-editable, there should be at least one valid value that the setting is
allowed to be. Optionally, you can specify the default value for any setting
and whether the setting is read-only. Additionally, read-only and default values
can depend on the value of another setting.

Settings are defined by writing any valid string name followed by a colon. Setting
parts come after the colon and are either comma-separated or are placed one per
line after the setting name. You can mix and match any number of comma-separated
parts and parts on their own lines. Whatever makes the most sense and is the most
readable is allowed.  Settings parts can show up in any order after the setting
name. You can define size, read-only, defaults and valid options in any order you
wish. The only restriction is that the size part MUST appear before any default parts.

Any line in a settings definition file that starts with a hashtag (`#`) is treated
as a comment. You can write anything you want in comments so feel free to write
down any useful information about settings you think somebody else might care to
know.

## A Simple Setting

The most basic setting is one that has a name, a size and some allowed values.
An example of such a setting is like so:

```
Sample Setting: byte, values are 1 to 10
```

This defines a setting named "Sample Setting" which is a single byte and can
have the hex values 01, 02, 03, 04, 05, 06, 07, 08, 09 and 10. Editors that
display this setting will display a drop-down or selection box that includes
the decimal values "1", "2", "3", "4", "5", "6", "7", "8", "9", and "10".
The decimal values for each valid setting is automatically inferred based on
the range given in the setting.

If you want to specify some alternate text for each valid setting, you may
do so like so:

```
Sample Setting: byte, 1 - On, 0 - Off
```

This defines a setting named "Sample Setting" which is a single byte and can
have the hex values 01 and 00 applied to it. Editors that display this setting
will display a drop-down or selection box that includes the value "On" and
"Off" and will select the correct one based on the value in the EEPROM when it
is parsed.

You can mix and match how you define settings values if it is most convenient.
For example, the following setting mixes the two ways of specifying valid
values:

```
Sample Setting: byte, 0 - Off, 1 to 9, 10 - MAX
```

This defines a setting named "Sample Setting" which is a single byte and
can have the hex values 00, 01, 02, 03, 04, 05, 06, 07, 08, 09 and 10. Editors
that display this setting will display a drop-down or selection box that includes
the options "Off", "1", "2", "3", "4", "5", "6", "7", "8", "9", "MAX". The
correct one will be selected based on the value in the EEPROM when it is parsed.

## Changing the Setting Size

If your setting spans more than 1 byte, or it is only the top half or bottom
half of a byte, you can specify that in the size part. For settings that occupy
more than 1 byte, you can simply write the number of bytes in the part section.
If a setting only occupies the top or bottom half of a byte, you can specify
a half-byte for the size.

An example of a setting that takes up 4 bytes is as follows:

```
Big Setting: 2 bytes, 12 34 - On, 56 78 - Off
```

This defines a setting named "Big Setting" that takes up two bytes and has
the two hex values 12 34 and 56 78 as read in a hex editor as its options.
Editors will display either "On" or "Off" as they would for 1 byte settings.

An example of a pair of settings that take up half a byte each is as follows:

```
Small Setting 1: half-byte, values are 1 to 2
Small Setting 2: half-byte, values are 3 to 4
```

This defines two settings named "Small Setting 1" and "Small Setting 2". Each
setting takes up half a byte. The first setting, "Small Setting 1", will take
the top half of the byte, and the second, "Small Setting 2", will take the
bottom half of the byte. The hex values for each are the same as they would
be for all other documented settings. Note that the settings came in a pair
because you have to specify both halves of the byte!

## Specifying Read-Only Settings

Sometimes there is a setting that you can't figure out, or there's a setting
that the game writes when it initializes the EEPROM but never changes. In this
case you can mark the setting read-only and editors will not let people see
or change the setting. However, the setting will still be created when somebody
needs to make a default EEPROM based on the settings definition file.

An example of how to mark a setting as read-only:

```
Hidden Setting: byte, read-only
```

In this case, there is a setting named "Hidden Setting" which is a single
byte. We specified that it was read-only, so editors will not display the
setting to the user. Also, since it was read-only, we didn't need to specify
any allowed values. You can use this when there are parts of the EEPROM you
don't want people to mess with, or that you don't understand so you just need
to skip it.

Sometimes there are settings that only display in some scenarios, such as when
another setting is set to a certain value. If you run into a setting such as
this, you can specify that relationship like so:

```
Sometimes Hidden Setting: byte, read-only if Other Setting is 1, values are 0-2
```

This defines a setting called "Sometimes Hidden Setting" which is a single byte
and can have the hex values 00, 01 and 02. When another setting named "Other
Setting" is set to 1, this setting becomes read-only and cannot be modified
by the user. When that other setting named "Other Setting" is set to any other
value, this setting becomes user-changeable.

If you want to specify that a setting is read-only unless another setting is
a certain value, you can do so like the following:

```
Sometimes Hidden Setting: byte, read-only unless Other Setting is 1, values are 0-2
```

This defines the same setting as the first example, but the read-only logic
is reversed. This setting will be read-only when "Other Setting" is any value
but 1, and will be user-changeable when "Other Setting" is 1.

If you need to specify multiple values for the other setting, you can do so
like so:

```
Sometimes Hidden Setting: byte, read-only if Other Setting is 1 or 2, values are 0-2
```

This defines the same setting as the first example, but the read-only logic
is changed. The setting will be read only when "Other Setting" is 1 or 2, and
will be user-changeable when "Other Setting" is any other value.

## Specifying Defaults

Its nice to specify what the default for each setting is. This way, editors
can make a new EEPROM from scratch for the game you are defining without needing
an EEPROM to exist first. If you don't specify a default, the default for the
setting is assumed to be 0. If that isn't a valid value for a setting, you'll
run into problems so it is best to define defaults for settings when you can.

To specify a default, you can do the following:

```
Default Setting: byte, default is 1, values are 1, 2
```

This defines a setting named "Defaut Setting" which is a single byte and whose
valid values are 01 and 02. The default value when creating an EEPROM from
scratch is 01.

If a setting is read-only, then when we an EEPROM is edited and saved, the
default value will take precidence over the current value. If a setting is
user-editable, then the current value will take precidence over the default
value. This is so that you can have settings which are optionally read-only
based on other settings and specify what value the setting should be when
it is read-only. This isn't often necessary but it can come in handy in some
specific scenarios.

For example, in Marvel Vs. Capcom 2, the "Continue" setting is defaulted to
"On". However, if event mode is turned on, then the "Continue" setting is
forced to "Off" and becomes no longer user-editable. To represent such
a case as this, you can do something like the following:

```
Event: byte, default is 0
  0 - Off
  1 - On
Continue: byte, read-only if Event is 1, default is 1 if Event is 0, default is 0 if Event is 1
  0 - Off
  1 - On
```

This can be a bit daunting to read at first, so let's break it down. First,
it defines a setting named "Event" which is a byte and can have values 00 and 01.
Those values are labelled "Off" and "On" respectively. Event mode is off by default.
Then, it defines a setting named "Continue" which is a byte as well. It has values
00 and 01 labelled "Off" and "On" respectively. It is user-editable when event mode
is off, and it is read-only when event mode is on. When event mode is off, the default
is 01, which corresponds to "On". When event mode is on, the default is "00" which
corresponds to "Off". Remember how settings that are read-only try to save the
default first, and settings that are user-changeable try to save the current value
first? That's where the magic happens. When the "Event" setting is set to "On"
then the "Continue" setting is read-only, so we will save the default hex value of 00!
When the "Event" setting is set to "Off", the "Continue" setting is user-changeable so
we will save whatever value the user selected! When we create a new EEPROM from scratch,
we set "Event" to 00 which tells the "Continue" setting to default to 01. It all works
perfectly!
