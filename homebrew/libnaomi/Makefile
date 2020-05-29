# Please see the README for setting up a valid build environment.

# We only need to build the libnaomi.a library here
all: libnaomi.a

# The source files that make libnaomi.a tick.
SRCS += crt0.s
SRCS += system.c
SRCS += video.c
SRCS += maple.c
SRCS += audio.c

# Pick up base makefile rules common to all examples.
include ../Makefile.base

# Special-case for AICA binary, since it is an ARM executable.
build/arm/base.o: base.s
	@mkdir -p $(dir $@)
	${ARMAS} -o $@ $<

build/arm/aica.o: aica.c
	@mkdir -p $(dir $@)
	${ARMCC} -c -o $@ $<

build/arm/aica.elf: build/arm/base.o build/arm/aica.o build/arm/success.o
	@mkdir -p $(dir $@)
	${ARMLD} -o $@ $^ ${ARMLIBS}

build/aica.bin: build/arm/aica.elf
	@mkdir -p $(dir $@)
	${ARMOBJCOPY} $< $@ -O binary

build/aica.bin.o: build/aica.bin
	@mkdir -p $(dir $@)
	${BIN2C} $<.c $<
	${CC} -c $<.c -o $@

build/arm/success.o: success.raw
	@mkdir -p $(dir $@)
	${BIN2C} build/arm/success.c $<
	${ARMCC} -c build/arm/success.c -o $@

# Package up all object files, created from the above sources.
libnaomi.a: ${OBJS} build/aica.bin.o
	$(AR) -rcs -o $@ $^

# Include a simple clean target which wipes the build directory
# and kills any library built.
.PHONY: clean
clean:
	rm -rf build
	rm -rf libnaomi.a
