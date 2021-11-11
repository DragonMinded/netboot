# Please see the README for setting up a valid build environment.

# We only need to build the libnaomi.a library here
all: libnaomimessage.a

# The source files that make libnaomimessage.a tick.
SRCS += packet.c
SRCS += message.c

# Pick up base makefile rules common to all examples.
include ../../Makefile.base

# Package up all object files, created from the above sources.
libnaomimessage.a: ${OBJS}
	$(AR) -rcs -o $@ $^

# Package up all the dependencies and such that libnaomi needs and put them
# into our install directory.
.PHONY: install
install: libnaomimessage.a
	mkdir -p ${NAOMI_SH_BASE}/lib
	cp libnaomimessage.a ${NAOMI_SH_BASE}/lib

# Include a simple clean target which wipes the build directory
# and kills any library built.
.PHONY: clean
clean:
	rm -rf build
	rm -rf libnaomimessage.a
