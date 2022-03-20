# settings

A collection of helper routines and classes that are used by naomi.settings
to provide high-level binary manipulation of EEPROM files. Note that while
these routines could be used to manipulate Naomi SRAM files or other arbitrary
binary data there has been no effort to generalize them further than the present.

Ideally this would have its own generic file read/write routines and such so
that it could be used with definitions files and any arbitrary binary data. However,
it currently is usuable only with the naomi-specific settings code. Perhaps sometime
in the future this will be fleshed out, tested and made into its own module?
