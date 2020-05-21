# Please see the README for setting up a valid build environment.

# The top-level binary that you wish to produce.
all: inputtest.bin

# All of the source files (.c and .s) that you want to compile.
# You can use relative directories here as well. Note that if
# one of these files is not found, make will complain about a
# missing missing `build/naomi.bin' target, so make sure all of
# these files exist.
SRCS += main.c

# Pick up base makefile rules common to all examples.
include ../../Makefile.base

# Provide the top-level ROM creation target for this binary.
# See scripts.makerom for details about what is customizable.
inputtest.bin: build/naomi.bin
	PYTHONPATH=../../../ python3 -m scripts.makerom $@ \
		--title "Cabinet Input Test" \
		--publisher "DragonMinded" \
		--section $<,0xc021000 \
		--entrypoint 0xc021000 \
		--test-entrypoint 0xc021000

# Include a simple clean target which wipes the build directory
# and kills any binary built.
.PHONY: clean
clean:
	rm -rf build
	rm -rf inputtest.bin
