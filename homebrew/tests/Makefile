# Please see the README for setting up a valid build environment.

# The top-level binary that you wish to produce.
all: tests.bin

# All of the source files (.c and .s) that you want to compile.
# You can use relative directories here as well. Note that if
# one of these files is not found, make will complain about a
# missing missing `build/naomi.bin' target, so make sure all of
# these files exist.
SRCS += build/testsuite.c
SRCS += build/aica_test.bin.o

# Pick up base makefile rules common to all examples.
include ../Makefile.base

# Auto-find test cases and add them to the binary.
TEST_SOURCES := $(wildcard *.c)

# Buildrule to generate the test suite from our template.
build/testsuite.c: generate.py ${TEST_SOURCES}
	@mkdir -p $(dir $@)
	python3 generate.py $@ ${TEST_SOURCES}

# Special-case for a test AICA binary, since it is an ARM executable.
# We write our own AICA stub here since we want to test the HW, so we
# dont want default functionality from libnaomi.
build/arm/arm-crt0.o: ../libnaomi/arm-crt0.s
	@mkdir -p $(dir $@)
	${ARMAS} -o $@ $<

build/arm/aica.o: aica.c
	@mkdir -p $(dir $@)
	${ARMCC} -c -o $@ $<

build/arm/aica.elf: build/arm/arm-crt0.o build/arm/aica.o
	@mkdir -p $(dir $@)
	${ARMLD} -o $@ $^ ${ARMLIBS}

build/aica_test.bin: build/arm/aica.elf
	@mkdir -p $(dir $@)
	${ARMOBJCOPY} $< $@ -O binary

build/aica_test.bin.o: build/aica_test.bin
	@mkdir -p $(dir $@)
	${BIN2C} $<.c $<
	${CC} -c $<.c -o $@

# Provide the top-level ROM creation target for this binary.
# See scripts.makerom for details about what is customizable.
tests.bin: build/naomi.bin
	PYTHONPATH=../../ python3 -m scripts.makerom $@ \
		--title "libNaomi Test Suite ROM" \
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
	rm -rf tests.bin
