all: zlib libbz2 libpng freetype libjpeg libxmp

.PHONY: zlib
zlib:
	$(MAKE) -C zlib install

.PHONY: libbz2
libbz2:
	$(MAKE) -C libbz2 install

.PHONY: libpng
libpng:
	$(MAKE) -C libpng install

.PHONY: freetype
freetype:
	$(MAKE) -C freetype install

.PHONY: libjpeg
libjpeg:
	$(MAKE) -C libjpeg install

.PHONY: libxmp
libxmp:
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
