all: build

# Download arguments
BASE_URL = https://download.savannah.gnu.org/releases/freetype/

NAME = freetype-2.10.2

ARTIFACT = ${NAME}.tar.gz

# Build arguments
USES_AUTOCONF = 1
NO_INSTALL = 1

CONFIGURE_ARGS = --host=sh-elf --enable-static --disable-mmap --with-brotli=no --with-harfbuzz=no

REQUIRED_LIBS = -lbz2 -lz -lpng16

BUILD_TARGET = all install

include ../Makefile.base
