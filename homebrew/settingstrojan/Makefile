# Please see the README for setting up a valid build environment.

# The top-level binary that you wish to produce.
all: settingstrojan.bin

# Include the trojan itself and debugging libraries.
SRCS += crt0.s
SRCS += trojan.c
SRCS += eeprom.c
SRCS += video.c

# Overwrite starting address with a sane new default where we stick our binary trojan.
START_ADDR = 0xDFC0000

# Don't use libnaomi, we need to be small and modify minimal hardware.
BARE_METAL = 1

# Pick up base makefile rules so we don't have to rewrite the makefile from scratch.
include ${NAOMI_BASE}/tools/Makefile.base

# We don't put a header on the raw binary since we will just shove it into an existing ROM.
settingstrojan.bin: build/naomi.bin
	cp $< $@

# Include a simple clean target which wipes the build directory
# and kills any binary built.
.PHONY: clean
clean:
	rm -rf build
