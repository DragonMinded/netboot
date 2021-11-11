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
SRCS += dejavusans.ttf

# Pick up base makefile rules common to all examples.
include ../Makefile.base

# Only link against freetype if it has itself been compiled.
ifeq ($(call libmissing,freetype), 0)
# Libraries that we want to link against.
LIBS += -lfreetype -lbz2 -lz -lpng16
endif

# Provide a rule to build our ROM FS.
build/romfs.bin: romfs/ ${ROMFSGEN_FILE}
	${ROMFSGEN} $@ $<

# Auto-find test cases and add them to the binary.
TEST_SOURCES := $(wildcard *.c)

# Buildrule to generate the test suite from our template.
build/testsuite.c: generate.py ${TEST_SOURCES}
	@mkdir -p $(dir $@)
	python3 generate.py $@ ${TEST_SOURCES}

# Special-case for a test AICA binary, since it is an ARM executable.
# We write our own AICA stub here since we want to test the HW, so we
# dont want default functionality from libnaomi.
build/aica/arm-crt0.o: ../libnaomi/aica/arm-crt0.s
	@mkdir -p $(dir $@)
	${ARMAS} -o $@ $<

build/aica/main.o: aica/main.c
	@mkdir -p $(dir $@)
	${ARMCC} -c -o $@ $<

build/aica/aica.elf: build/aica/arm-crt0.o build/aica/main.o
	@mkdir -p $(dir $@)
	${ARMLD} -o $@ $^ ${ARMLIBS}

build/aica_test.bin: build/aica/aica.elf
	@mkdir -p $(dir $@)
	${ARMOBJCOPY} $< $@ -O binary

build/aica_test.bin.o: build/aica_test.bin ${BIN2C_FILE}
	@mkdir -p $(dir $@)
	${BIN2C} $<.c $<
	${CC} -c $<.c -o $@

# Provide the top-level ROM creation target for this binary.
# See scripts/makerom.py for details about what is customizable.
tests.bin: ${MAKEROM_FILE} ${NAOMI_BIN_FILE} build/romfs.bin
	${MAKEROM} $@ \
		--title "libNaomi Test Suite ROM" \
		--publisher "DragonMinded" \
		--serial "${SERIAL}" \
		--section ${NAOMI_BIN_FILE},${START_ADDR} \
		--entrypoint ${MAIN_ADDR} \
		--main-binary-includes-test-binary \
		--test-entrypoint ${TEST_ADDR} \
		--align-before-data 4 \
		--filedata build/romfs.bin

# Include a simple clean target which wipes the build directory
# and kills any binary built.
.PHONY: clean
clean:
	rm -rf build
	rm -rf tests.bin
