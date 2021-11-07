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
SRCS += maple.c
SRCS += eeprom.c
SRCS += audio.c
SRCS += console.c
SRCS += rtc.c

# Pick up base makefile rules common to all examples.
include ../Makefile.base

# Special-case for AICA binary, since it is an ARM executable. This is
# stored in the main executable as the default AICA binary which can be
# loaded by homebrew wishing to use sound.
build/aica/arm-crt0.o: aica/arm-crt0.s
	@mkdir -p $(dir $@)
	${ARMAS} -o $@ $<

# This has a bunch of debugging in it still and would need to be cleaned
# up before we consider the AICA binary production-ready.
build/aica/main.o: aica/main.c
	@mkdir -p $(dir $@)
	${ARMCC} -c -o $@ $<

build/aica/pitchtable.o: ${PITCHGEN_FILE}
	@mkdir -p $(dir $@)
	${PITCHGEN} build/aica/pitchtable.c 128
	${ARMCC} -c build/aica/pitchtable.c -o $@

build/aica/aica.elf: build/aica/arm-crt0.o build/aica/main.o build/aica/pitchtable.o build/aica/success.o
	@mkdir -p $(dir $@)
	${ARMLD} -o $@ $^ ${ARMLIBS}

build/aica.bin: build/aica/aica.elf
	@mkdir -p $(dir $@)
	${ARMOBJCOPY} $< $@ -O binary

build/aica.bin.o: build/aica.bin ${BIN2C_FILE}
	@mkdir -p $(dir $@)
	${BIN2C} $<.c $<
	${CC} -c $<.c -o $@

# This is a simple test sound that I never did get playing on actual HW.
# It seems to work in MAME however, so I suspect there's a problem with
# what memory it is placed into, perhaps it needs to be copied to somewhere
# else before I try playing it. Eventually, this should be deleted and
# the default AICA binary should be usable to request sounds be played from
# the main executable through some sort of RPC.
build/aica/success.o: aica/success.raw ${BIN2C_FILE}
	@mkdir -p $(dir $@)
	${BIN2C} build/aica/success.c $<
	${ARMCC} -c build/aica/success.c -o $@

# Package up all object files, created from the above sources.
libnaomi.a: ${OBJS} build/aica.bin.o
	$(AR) -rcs -o $@ $^

# Include a simple clean target which wipes the build directory
# and kills any library built.
.PHONY: clean
clean:
	rm -rf build
	rm -rf libnaomi.a
