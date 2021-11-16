# Please see the README for setting up a valid build environment.

# We only need to build the libnaomi.a library here
all: libnaomi.a

# The source files that make libnaomi.a tick.
SRCS += sh-crt0.s
SRCS += system.c
SRCS += interrupt.c
SRCS += timer.c
SRCS += thread.c
SRCS += dimmcomms.c
SRCS += video.c
SRCS += video-freetype.c
SRCS += ta.c
SRCS += maple.c
SRCS += eeprom.c
SRCS += audio.c
SRCS += console.c
SRCS += rtc.c
SRCS += cart.c
SRCS += romfs.c
SRCS += matrix.c

# Pick up base makefile rules common to all examples.
include ../Makefile.base

# Special-case for AICA binary, since it is an ARM executable. This is
# stored in the main executable as the default AICA binary which can be
# loaded by homebrew wishing to use sound.
build/aica/arm-crt0.o: aica/arm-crt0.s
	@mkdir -p $(dir $@)
	${ARMAS} -o $@ $<

build/aica/main.o: aica/main.c
	@mkdir -p $(dir $@)
	${ARMCC} -c -o $@ $<

build/aica/clib.o: aica/clib.c
	@mkdir -p $(dir $@)
	${ARMCC} -c -o $@ $<

build/aica/pitchtable.o: ${PITCHGEN_FILE}
	@mkdir -p $(dir $@)
	${PITCHGEN} build/aica/pitchtable.c 128
	${ARMCC} -c build/aica/pitchtable.c -o $@

build/aica/aica.elf: build/aica/arm-crt0.o build/aica/main.o build/aica/clib.o build/aica/pitchtable.o
	@mkdir -p $(dir $@)
	${ARMLD} -o $@ $^ ${ARMLIBS}

build/aica.bin: build/aica/aica.elf
	@mkdir -p $(dir $@)
	${ARMOBJCOPY} $< $@ -O binary

build/aica.bin.o: build/aica.bin ${BIN2C_FILE}
	@mkdir -p $(dir $@)
	${BIN2C} $<.c $<
	${CC} -c $<.c -o $@

# Package up all object files, created from the above sources.
libnaomi.a: ${OBJS} build/aica.bin.o
	$(AR) -rcs -o $@ $^

# Package up all the dependencies and such that libnaomi needs and put them
# into our install directory.
.PHONY: install
install: libnaomi.a
	mkdir -p ${NAOMI_SH_BASE}/lib
	cp libnaomi.a ${NAOMI_SH_BASE}/lib
	mkdir -p ${NAOMI_SH_BASE}/include
	cp -r naomi ${NAOMI_SH_BASE}/include
	cp -r sys ${NAOMI_SH_BASE}/include

# Include a simple clean target which wipes the build directory
# and kills any library built.
.PHONY: clean
clean:
	rm -rf build
	rm -rf libnaomi.a
