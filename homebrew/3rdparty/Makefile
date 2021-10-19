all: zlib libbz2 libpng libjpeg

.PHONY: zlib
zlib:
	$(MAKE) -C zlib install

.PHONY: libbz2
libbz2:
	$(MAKE) -C libbz2 install

.PHONY: libpng
libpng:
	$(MAKE) -C libpng install

.PHONY: libjpeg
libjpeg:
	$(MAKE) -C libjpeg install

.PHONY: clean
clean:
	$(MAKE) -C zlib clean
	$(MAKE) -C libbz2 clean
	$(MAKE) -C libpng clean
	$(MAKE) -C libjpeg clean

.PHONY: allclean
allclean:
	$(MAKE) -C zlib allclean
	$(MAKE) -C libbz2 allclean
	$(MAKE) -C libpng allclean
	$(MAKE) -C libjpeg allclean
