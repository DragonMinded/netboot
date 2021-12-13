# Please see the README for setting up a valid build environment.

# The top-level binary that you wish to produce.
all: netbootmenu.bin

# All of the source files (.c and .s) that you want to compile.
# You can use relative directories here as well. Note that if
# one of these files is not found, make will complain about a
# missing missing `build/naomi.bin' target, so make sure all of
# these files exist.
SRCS += main.c
SRCS += config.c
SRCS += controls.c
SRCS += screens.c
SRCS += dejavusans.ttf
SRCS += scroll.raw
SRCS += check.raw
SRCS += change.raw
SRCS += build/up.o
SRCS += build/dn.o
SRCS += build/cursor.o

# Libraries we need to link against.
LIBS += -lnaomimessage -lfreetype -lbz2 -lz -lpng16

# Override the default serial so this program has its own settings.
SERIAL = BRP0

# Pick up base makefile rules common to all examples.
include ../Makefile.base

# Specific buildrule for PNG files for this project. Note that these
# *MUST* be a multiple of 8 both in width and height.
build/%.o: %.png ${IMG2C_FILE}
	@mkdir -p $(dir $@)
	${IMG2C} build/$<.c --mode RGBA1555 $<
	${CC} -c build/$<.c -o $@

# Provide the top-level ROM creation target for this binary.
# See scripts/makerom.py for details about what is customizable.
netbootmenu.bin: ${MAKEROM_FILE} ${NAOMI_BIN_FILE}
	${MAKEROM} $@ \
		--title "Net Boot ROM Picker Menu" \
		--publisher "DragonMinded" \
		--serial "${SERIAL}" \
		--section ${NAOMI_BIN_FILE},${START_ADDR} \
		--entrypoint ${MAIN_ADDR} \
		--main-binary-includes-test-binary \
		--test-entrypoint ${TEST_ADDR}

# Include a simple clean target which wipes the build directory
# and kills any binary built.
.PHONY: clean
clean:
	rm -rf build
	rm -rf netbootmenu.bin
