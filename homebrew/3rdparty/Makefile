all: zlib libbz2 libpng freetype libjpeg libxmp

.PHONY: zlib
zlib:
	$(MAKE) -C zlib

.PHONY: libbz2
libbz2:
	$(MAKE) -C libbz2

.PHONY: libpng
libpng:
	$(MAKE) -C libpng

.PHONY: freetype
freetype:
	$(MAKE) -C freetype

.PHONY: libjpeg
libjpeg:
	$(MAKE) -C libjpeg

.PHONY: libxmp
libxmp:
	$(MAKE) -C libxmp

install: install-zlib install-libbz2 install-libpng install-freetype install-libjpeg install-libxmp

.PHONY: install-zlib
install-zlib:
	$(MAKE) -C zlib install

.PHONY: install-libbz2
install-libbz2:
	$(MAKE) -C libbz2 install

.PHONY: install-libpng
install-libpng:
	$(MAKE) -C libpng install

.PHONY: install-freetype
install-freetype:
	$(MAKE) -C freetype install

.PHONY: install-libjpeg
install-libjpeg:
	$(MAKE) -C libjpeg install

.PHONY: install-libxmp
install-libxmp:
	$(MAKE) -C libxmp install

.PHONY: clean
clean:
	$(MAKE) -C zlib clean
	$(MAKE) -C libbz2 clean
	$(MAKE) -C libpng clean
	$(MAKE) -C freetype clean
	$(MAKE) -C libjpeg clean
	$(MAKE) -C libxmp clean

.PHONY: allclean
allclean:
	$(MAKE) -C zlib allclean
	$(MAKE) -C libbz2 allclean
	$(MAKE) -C libpng allclean
	$(MAKE) -C freetype allclean
	$(MAKE) -C libjpeg allclean
	$(MAKE) -C libxmp allclean
