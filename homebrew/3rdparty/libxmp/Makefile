all: build

# Download arguments
BASE_URL = https://github.com/libxmp/libxmp/archive/refs/tags

NAME = libxmp-xmp-4.0.6

ARTIFACT = xmp-4.0.6.zip

# Build arguments
USES_AUTOCONF = 1
GENERATE_CONFIGURE = 1

CONFIGURE_ARGS = --host=sh-elf --enable-static

DEFINES = -DPATH_MAX=244 -DWIN32=1 -DHAVE_POPEN=1

BUILD_TARGET = lib/libxmp.a

INSTALL_HEADERS = include/xmp.h

include ../Makefile.base
