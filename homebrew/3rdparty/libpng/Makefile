all: build

# Download arguments
BASE_URL = ftp://ftp.simplesystems.org/pub/libpng/png/src/libpng16

NAME = libpng-1.6.37

ARTIFACT = ${NAME}.tar.gz

# Build arguments
USES_AUTOCONF = 1

CONFIGURE_ARGS = --host=sh-elf --enable-static

REQUIRED_LIBS = -lz

BUILD_TARGET = libpng16.la

INSTALL_LIBS = .libs/libpng16.a

INSTALL_HEADERS = png.h pngconf.h pnglibconf.h

HEADER_INSTALLDIR = png

include ../Makefile.base
