all: zlib libbz2 libpng

.PHONY: zlib
zlib:
	$(MAKE) -C zlib install

.PHONY: libbz2
libbz2:
	$(MAKE) -C libbz2 install

.PHONY: libpng
libpng:
	$(MAKE) -C libpng install

.PHONY: clean
clean:
	$(MAKE) -C zlib clean
	$(MAKE) -C libbz2 clean
	$(MAKE) -C libpng clean
