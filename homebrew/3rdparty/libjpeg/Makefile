all: build

# Download arguments
BASE_URL = http://ijg.org/files

NAME = jpeg-9d

ARTIFACT = jpegsrc.v9d.tar.gz

# Build arguments
USES_AUTOCONF = 1

CONFIGURE_ARGS = --host=sh-elf --enable-static

BUILD_TARGET = libjpeg.la

INSTALL_LIBS = .libs/libjpeg.a

INSTALL_HEADERS = jconfig.h jmorecfg.h jpeglib.h jpegint.h jerror.h

HEADER_INSTALLDIR = jpeg

include ../Makefile.base
