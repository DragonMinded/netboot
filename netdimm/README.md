# netdimm

Collection of routines written in Python for remotely controlling a SEGA Net Dimm
attached to a Naomi, Triforce or Chihiro system. Originally based off of the old
triforcetools.py script floating around the web, this has been upgraded to Python 3,
fully typed and massively improved. It requires a minimum of Python 3.6 to operate.

## NetDimm

The NetDimm class provides high-level access to a net dimm accessible from the network.
It handles uploading and downloading data, querying status and changing modes of the
net dimm.

### Default Constructor

Takes a single string containing thet IP of the net dimm you wish to connect to and
manages that connection. All query and update methods found in this class can be
called directly and they will connect to the net dimm, perform their action and then
disconnect. However, it can be faster to maintain that connection. For that, see the
`connection()` context manager below. Optionally, the second argument (or keyword
argument) version can be given. This is a `NetDimmVersionEnum` which allows you to
set the version of the net dimm you are talking to. This matters in a few rare cases.
Optionally, the third argument (or keyword argument) log can be given. This can either
be a function in the form of `log(msg: str, *, newline: bool = True) -> None` or it can
be given the `print` function. In either case, if this is provided, various verbose
information will be logged.

### crc() static method

Takes either a bytes or a `FileBytes` object and runs a CRC over the entire contents
in the same way that the net dimm would CRC the same data. Use this to calculate what
the expected CRC should be for a given chunk of data you might wish to upload to a net
dimm or to compare against the CRC returned in an `info()` call to see if what you
want to send is already running on the net dimm.

### connection() context manager

Takes no arguments, and when run like `with inst.connection():` will manage the connection
to the net dimm for you. You do not need to use this function. However, if you are issuing
many commands to the net dimm in a row, it is much faster to wrapp all of those function
calls in a connection in order to remove the time it takes to connect and disconnect between
every command.

### info() method

Returns a `NetDimmInfo` containing information about the net dimm you have pointed at. The
`NetDimmInfo` object has the following properties. The `current_game_crc` is an integer
representing the CRC of the game currently running on the net dimm. Compare it to the output
of the `crc()` static method for the same data. The `current_game_size` is an integer
representing the size in bytes of the game currently running on the net dimm. The
`game_crc_status` property is a `CRCStatusEnum` representing the current status of the
CRC on the system, such as `CRCStatusEnum.STATUS_VALID` or `CRCStatusEnum.STATUS_INVALID`.
This allows you to check whether the net dimm thinks the onboard CRC matches the onboard data.
The `memory_size` property is an integer representing the number of megabytes of RAM installed
in the net dimm. The `available_game_memory` represents the maximum size in bytes of a
game that may be stored on the net dimm. The `firmware_version` property is a
`NetDimmVersionEnum` representing the version of the net dimm firmware running. Note that
when you call the `info()` method, the version property on your net dimm instance will be
updated accordingly.

### send() method

Send a game to the net dimm. Takes a data argument which can either be bytes or `FileBytes`
and sends it to the net dimm. This also takes care of setting the net dimm information
and setting the onbaord DES key. If you give the optional key argument, that key will
be used to encrypt the game data as well as set the crypto key on the net dimm. By
default you do not need to use this. If you give the optional boolean disable_crc_check
argument then the net dimm will not CRC the data after you send it and will instead boot
directly into it. When this mode is set, the `game_crc_status` returned from the `info()`
call will have a value of `CRCStatusEnum.STATUS_DISABLED`. If you give it the optional
progress_callback argument in the form of a function that takes two integer parameters
and returns nothing, then this function will call that function periodically with the
current send location and the send size in bytes to inform you of the send progress which
can take awhile.

### receive() method

Receive a previously sent game from the net dimm. Ensures that the game itself on the net
dimm is valid and then downloads the entire contents before returning it as bytes. If the
game could not be retrieved because it has an invalid CRC or there is no game installed,
this returns None. Much like the `send()` method, this takes an optional progress_callback
argument in the form of a function with two integer parameters and returning nothing. If
this is provided, the callack will be called periodically with the current location and
receive size of the game being downloaded.

### reboot() method

Reboot the net dimm after sending a game in order to boot the game. This can be issued at
any time, but it makes most sense to do so after finishing an upload.

### peek() method

Given an address as an integer and a type in the form of either `PeekPokeTypeEnum.TYPE_BYTE`,
`PeekPokeTypeEnum.TYPE_SHORT` or `PeekPokeTypeEnum.TYPE_LONG`, attempts to read that size
of data from that address from the target system running the net dimm. Returns the actual
value or 0 if it could not be retrieved.

### poke() method

Given an address as an integer, a type in the form of either `PeekPokeTypeEnum.TYPE_BYTE`,
`PeekPokeTypeEnum.TYPE_SHORT` or `PeekPokeTypeEnum.TYPE_LONG` and a data value, attempts
to write that size of data to that address on the target system running the net dimm.
