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
Optionally, the third argument (or keyword argument) target can be given. This is a
`NetDimmTargetEnum` which allows you to set the target the net dimm is talking to. This
matters for default timeouts. Optionally, the forth argument (or keyword argument) log
can be given. This can either be a function in the form of
`log(msg: str, *, newline: bool = True) -> None` or it can be given the `print` function.
In either case, if this is provided, various verbose information will be logged.
Optionally, the firth argument (or keyword argument) timeout can be given. This should
be an integer representing the number of seconds before a send or receive should time
out when the net dimm does not talk. This is normally determined automatically given
a correct target keyword but you can also specify it manually.

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

### send_chunk() method

Send a chunk of binary data to the net dimm, stored at an offset. Takes two parameters, the
first being the integer offset from the beginning of the net dimm writeable memory to put
the binary data, and the second being either bytes or `FileBytes` and sends the entire
chunk to that location. Note that this is not to be used to upload game data as it does
not attempt to calculate or update the CRC. Using this to change data within the CRC'd
section of a valid sent game will cause it to become invalid on the next boot. If you
send data to a running game, you can subsequently read that data using the cartridge
read interface on running target.

### receive_chunk() method

Receive a chunk of binary data from the net dimm. Takes two parameters, the first being
an integer offset from the beginning of the net dimm writeable memory to get the binary
data, and the second being an integer length in bytes of the amount of data to receive.
Note that if you use the cartridge write interface on the running target to write data
to the cart, it will be available to read using this function.

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

## Naomi Homebrew Messaging Protocol

The netdimm module provides a series of functions that are capable of talking to a
Naomi homebrew program through a net dimm. Both low level packet-based and slightly
higher-level message-based functions are provided for you depending on your needs.
They correspond to the functions implemented in `naomi/message/packet.h` and
`naomi/message/message.h` respectively. The packet-based interface provides the
ability to read or write one packet at a time, read from or write to two scratch
registers and read from a configuration register. The message-based interface
provides the ability to send or receive optionally-compressed messages (binary
data with a type) which can be up to 64kb in size.

The `MAX_MESSAGE_LENGTH` constant gives you the size that you should not exceed
when sending messages, and the `MAX_PACKET_LENGTH` constant gives you the size you
should not exceed when sending packets.

The `Message` class is available for you when sending and receiving messages. Its
constructor takes a type argument which should be an integer in the range 0x0-0x7FFF
and optionally a data argument containing up to MAX_MESSAGE_LENGTH bytes as the
message payload. The packet type is available on instantiated `Message` classes
using the `id` attribute, and optional data is available on the `data` attribute.
Note that when a message contains no data, the `data` attribute will be Null.

The protocol is entirely host-driven. The Naomi program will not discard or attempt
to send or receive a packet or message without the host driving it. This is because
the Naomi has no way of requesting a net dimm send a packet, but the host has the
ability to request peek and poke messages that are performed on the Naomi's main
RAM. Thus, it is your responsibility to call either `receive_packet` or `receive_message`
in an event loop in order to keep the Naomi ROM's buffers from filling.

### send_packet

Takes an instantiated `NetDimm` class and a bytes object representing between 1
and 253 bytes of data to send to the Naomi program. Returns True if the packet
was successfully sent or False otherwise.

### receive_packet

Takes an instantiated `NetDimm` class and attempts to receive a single packet
between 1 and 253 bytes long from the Naomi program. If successful, the byte
data inside the packet will be returned. Otherwise, None is returned.

### read_scratch1_register

Attempts to read the 32-bit scratch1 register (usable for anything you want).
Returns the 32-bit value on success or None if the register could not be read.

### read_scratch2_register

Attempts to read the 32-bit scratch2 register (usable for anything you want).
Returns the 32-bit value on success or None if the register could not be read.

### write_scratch1_register

Attempts to write an integer parameter to the 32-bit scratch1 register. There
is no checking that this operation succeeded, though it generally does. If you
wish to be sure, you can read back the contents.

### write_scratch2_register

Attempts to write an integer parameter to the 32-bit scratch2 register. There
is no checking that this operation succeeded, though it generally does. If you
wish to be sure, you can read back the contents.

### send_message

Takes an instantiated `NetDimm` class and an instance of `Message` and attempts
to send that message to a Naomi program. Raises `MessageException` on failure
to send the message. This can happen if the Naomi program isn't running the message
protocol or if the program has crashed.

### receive_message

Takes an instantiated `NetDimm` class and attempts to receive a message from a
Naomi program. Raises `MessageException ` on critical failures, such as malformed
packets or if the Naomi program isn't running the message protocol. Returns an
instance of `Message` representing the received message on success, and returns
None if there was no message ready to receive.

Note that correctly configured Naomi homebrew programs that have installed the
stdio redirect hooks to send stdout and stderr to a communicating host will send
message of type `MESSAGE_HOST_STDOUT` and `MESSAGE_HOST_STDERR` when the respective
streams have data. These are flushed when a newline is received and contain that
newline in the data. To correctly display these, it is recommended to decode them
as utf-8 data and display them verbatum.
