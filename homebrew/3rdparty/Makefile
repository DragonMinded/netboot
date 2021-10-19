all: zlib libbz2

.PHONY: zlib
zlib:
	$(MAKE) -C zlib install

.PHONY: libbz2
libbz2:
	$(MAKE) -C libbz2 install

.PHONY: clean
clean:
	$(MAKE) -C zlib clean
	$(MAKE) -C libbz2 clean
