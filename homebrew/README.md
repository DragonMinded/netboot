A minimal Naomi homebrew environment, very loosely based off of KallistiOS toolchain work but bare metal. This assumes that you have a Linux computer with standard prerequisites for compiling gcc/newlib/binutils already installed. The Naomi system library is minimal, but will continue to get fleshed out. There is currently no support for threads though I hope to fix that at a later time as well.

To get started, create a directory named "/opt/toolchains/naomi" and copy the contents of the `setup/` directory to it. This directory and the copied contents should be user-owned and user-writeable. Then, cd to "/opt/toolchains/naomi" and in order run `./download.sh` (downloads toolchain sources), `./unpack.sh` (unpacks the toolchain to be built), `make` (builds the toolchain and installs it in the correct directories) and finally `./cleanup.sh`. If everything is successful, you should have a working environment.

The next thing you will need to do is build libnaomi, the system support library that includes the C/C++ runtime setup, newlib system hooks and various low-level drivers. To do that, run `make` from inside the `libnaomi/` directory. If a `libnaomi.a` file is created this means that the toolchain is set up properly and the system library was successfully built! If you receive error messages about "Command not found", you have not activated your Naomi enviornment by running `source /opt/toolchains/naomi/env.sh`.

To build any examples that are included, first activate the Naomi enviornment by running `source /opt/toolchains/naomi/env.sh`, and then running `make` in the directory of the example you want to run. The resulting binary file can be loaded in Demul or netbooted to a Naomi with a netdimm.

If you are looking for a great resource for programming, the first thing I would recommend is https://github.com/Kochise/dreamcast-docs which is mostly relevant to the Naomi. For memory maps and general low-level stuff, Mame's https://github.com/mamedev/mame/blob/master/src/mame/drivers/naomi.cpp is extremely valuable.

TODOs
=====
 - Figure out why audio doesn't play in ARM code, get a working sound example published.
 - Verify G1 functionality, add functionality for DMA from cartridge space.
 - Fill out more of the TODOs in system.c to add functionality such as a ROMFS and debug console.
 - Get libgcc/newlib compiled with threads enabled, publish a multi-threading example.
 - Use PowerVR accelerated texture commands instead of raw framebuffer.
