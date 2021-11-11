# Please see the README for setting up a valid build environment.

# The top-level binary that you wish to produce.
all: helloworld.bin

# All of the source files (.c and .s) that you want to compile.
# You can use relative directories here as well. Note that if
# one of these files is not found, make will complain about a
# missing missing `build/naomi.bin' target, so make sure all of
# these files exist.
SRCS += main.c

# Pick up base makefile rules so we don't have to do it by hand.
include ${NAOMI_BASE}/tools/Makefile.base

# Provide the top-level ROM creation target for this binary.
# See scripts.makerom for details about what is customizable.
helloworld.bin: ${MAKEROM_FILE} build/naomi.bin
	${MAKEROM} $@ \
		--title "Hello World" \
		--publisher "Your Name Here" \
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
	rm -rf helloworld.bin
