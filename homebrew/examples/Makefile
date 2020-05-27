all: hellonaomi hellocpp inputtest

.PHONY: hellonaomi
hellonaomi:
	$(MAKE) -C hellonaomi

.PHONY: hellocpp
hellocpp:
	$(MAKE) -C hellocpp

.PHONY: inputtest
inputtest:
	$(MAKE) -C inputtest

.PHONY: clean
clean:
	$(MAKE) -C hellonaomi clean
	$(MAKE) -C hellocpp clean
	$(MAKE) -C inputtest clean
