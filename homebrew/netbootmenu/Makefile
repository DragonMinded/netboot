# Please see the README for setting up a valid build environment.

# The top-level binary that you wish to produce.
all: netbootmenu.bin

# All of the source files (.c and .s) that you want to compile.
# You can use relative directories here as well. Note that if
# one of these files is not found, make will complain about a
# missing missing `build/naomi.bin' target, so make sure all of
# these files exist.
SRCS += main.c build/font.o build/up.o build/dn.o build/cursor.o

# Libraries we need to link against.
LIBS += -lfreetype -lbz2 -lz -lpng16

# Override the default serial so this program has its own settings.
SERIAL = BRP0

# Pick up base makefile rules common to all examples.
include ../Makefile.base

build/font.o: helvetica.ttf
	@mkdir -p $(dir $@)
	${BIN2C} build/font.c $<
	${CC} -c build/font.c -o $@

build/up.o: up.png
	@mkdir -p $(dir $@)
	${IMG2C} build/up.c --depth 2 $<
	${CC} -c build/up.c -o $@

build/dn.o: dn.png
	@mkdir -p $(dir $@)
	${IMG2C} build/dn.c --depth 2 $<
	${CC} -c build/dn.c -o $@

build/cursor.o: cursor.png
	@mkdir -p $(dir $@)
	${IMG2C} build/cursor.c --depth 2 $<
	${CC} -c build/cursor.c -o $@

# Provide the top-level ROM creation target for this binary.
# See scripts.makerom for details about what is customizable.
netbootmenu.bin: build/naomi.bin
	PYTHONPATH=../../ python3 -m scripts.makerom $@ \
		--title "Net Boot ROM Picker Menu" \
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
	rm -rf netbootmenu.bin
