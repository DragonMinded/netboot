all: hellonaomi hellocpp inputtest eepromtest

.PHONY: hellonaomi
hellonaomi:
	$(MAKE) -C hellonaomi

.PHONY: hellocpp
hellocpp:
	$(MAKE) -C hellocpp

.PHONY: inputtest
inputtest:
	$(MAKE) -C inputtest

.PHONY: eepromtest
eepromtest:
	$(MAKE) -C eepromtest

.PHONY: copy
copy: hellonaomi hellocpp inputtest eepromtest
	cp -r hellonaomi/hellonaomi.bin ../binaries/
	cp -r hellocpp/hellocpp.bin ../binaries/
	cp -r inputtest/inputtest.bin ../binaries/
	cp -r eepromtest/eepromtest.bin ../binaries/

.PHONY: clean
clean:
	$(MAKE) -C hellonaomi clean
	$(MAKE) -C hellocpp clean
	$(MAKE) -C inputtest clean
	$(MAKE) -C eepromtest clean
