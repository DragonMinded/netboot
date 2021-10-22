# Please see the README for setting up a valid build environment.

# The top-level binary that you wish to produce.
all: settingstrojan.bin

# Include the assembly file for the trojan itself.
SRCS += trojan.c

# Overwrite starting address with a sane new default where we stick our binary trojan.
START_ADDR = 0xDFC0000

# Pick up base makefile rules so we don't have to rewrite the makefile from scratch.
include ../Makefile.base

# Make sure the start address is saved in the executable so we can grab it in the injection script.
CC += -DSTART_ADDR=${START_ADDR}

# We don't put a header on the raw binary since we will just shove it into an existing ROM.
settingstrojan.bin: build/naomi.bin
	cp $< $@

# Include a simple clean target which wipes the build directory
# and kills any binary built.
.PHONY: clean
clean:
	rm -rf build
