# Please see the README for setting up a valid build environment.

# The top-level binary that you wish to produce.
all: debugprint.bin

# All of the source files (.c and .s) that you want to compile.
# You can use relative directories here as well. Note that if
# one of these files is not found, make will complain about a
# missing missing `build/naomi.bin' target, so make sure all of
# these files exist.
SRCS += main.c

# Pick up base makefile rules common to all examples.
include ../../Makefile.base

# We are using the naomi message library, so link it.
LIBS += -lnaomimessage

# Only link against zlib if it has itself been compiled.
# libnaomimessage will pick it up and use it if its available.
ifeq ($(call libmissing,z), 0)
# Libraries that we want to link against.
LIBS += -lz
endif

# Provide the top-level ROM creation target for this binary.
# See scripts.makerom for details about what is customizable.
debugprint.bin: build/naomi.bin
	PYTHONPATH=../../../ python3 -m scripts.makerom $@ \
		--title "STDIO Hook Example" \
		--publisher "DragonMinded" \
		--serial "${SERIAL}" \
		--section $<,${START_ADDR} \
		--entrypoint ${MAIN_ADDR} \
		--main-binary-includes-test-binary \
		--test-entrypoint ${TEST_ADDR}

# Include a simple clean target which wipes the build directory
# and kills any binary built.
.PHONY: clean
clean:
	rm -rf build
	rm -rf debugprint.bin
