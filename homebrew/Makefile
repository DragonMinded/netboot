all: libnaomi libnaomimessage examples tests

.PHONY: libnaomi
libnaomi:
	$(MAKE) -C libnaomi

.PHONY: libnaomimessage
libnaomimessage:
	$(MAKE) -C libnaomi/message

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
	$(MAKE) -C libnaomi/message clean
	$(MAKE) -C examples clean
	$(MAKE) -C tests clean
