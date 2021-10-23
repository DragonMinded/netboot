all: libnaomi examples tests

.PHONY: libnaomi
libnaomi:
	$(MAKE) -C libnaomi

.PHONY: examples
examples:
	$(MAKE) -C examples

.PHONY: tests
tests:
	$(MAKE) -C tests

.PHONY: 3rdparty
3rdparty:
	$(MAKE) -C 3rdparty

.PHONY: copy
copy: libnaomi examples
	$(MAKE) -C examples copy

.PHONY: clean
clean:
	$(MAKE) -C libnaomi clean
	$(MAKE) -C examples clean
	$(MAKE) -C tests clean
