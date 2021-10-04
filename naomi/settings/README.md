# naomi.settings

Collection of routines written in Python for safe manipulation of 128-byte
Naomi EEPROM files using supplied system definition files. Essentially, given
a valid 128-byte EEPROM or a valid 4-byte Naomi ROM serial and a set of system
and game definition files, `naomi.settings` will provide you a high-level
representation of valid settings including their defaults, valid values and
relationships to each other. Settings editors can be built using this module
which work together with `naomi.NaomiEEPRom` and `naomi.NaomiSettingsPatcher`
to make the settings available when netbooting a game on a Naomi system.
