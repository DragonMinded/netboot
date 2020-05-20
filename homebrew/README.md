A minimal Naomi homebrew environment, very loosely based off of KallistiOS toolchain work but bare metal. This assumes that you have a Linux computer with standard prerequisites for compiling gcc/newlib/binutils already installed. It does not provide any libraries for accessing Naomi-related hardware, thought that will come at a later time. It also does not currently support threads, though I hope to fix that at a later time as well.

To get started, create a directory named "/opt/toolchains/naomi" and copy the contents of the `setup/` directory to it. This directory should be user-owned and writeable. Then, cd to "/opt/toolchains/naomi" and in order run `./download.sh` (downloads toolchain sources), `./unpack.sh` (unpacks the toolchain to be built), `make` (builds the toolchain and installs it in the correct directories) and finally `./cleanup.sh`. If everything is successful, you should have a working environment.

To build any examples that are included, first activate the Naomi enviornment by running `source /opt/toolchains/naomi/env.sh`, and then running `make` in the directory of the example you want to run. The resulting binary file can be loaded in Demul or netbooted to a Naomi with a netdimm.

TODOs
=====
 - Generalize ldscript, crt0 and system.c into a support library.
 - Get started on implementing maple drivers for control input, verify the ARM side of the library.
 - Fill out more of the TODOs in system.c to add functionality such as a ROMFS and support for loading things from a cart.
 - Finish the crt0 script so it calls C init/fini as well as C++ global constructors/destructors.
