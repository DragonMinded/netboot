all: libnaomi examples

.PHONY: libnaomi
libnaomi:
	$(MAKE) -C libnaomi

.PHONY: examples
examples:
	$(MAKE) -C examples

.PHONY: clean
clean:
	$(MAKE) -C libnaomi clean
	$(MAKE) -C examples clean
