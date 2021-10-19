all: build

include ../Makefile.base

# Download arguments
VERSION = 1.2.11

BASE_URL = http://zlib.net/

DIRECTORY = zlib-${VERSION}

ARTIFACT = ${DIRECTORY}.tar.gz

# Build arguments
CONFIGURE_ARGS = --static

BUILD_TARGET = libz.a

INSTALL_TARGET = install

download/${ARTIFACT}:
	mkdir -p download
	wget http://zlib.net/${ARTIFACT} -P download

build/${DIRECTORY}: download/${ARTIFACT}
	mkdir -p build
	tar -xvzf download/${ARTIFACT} -C build
