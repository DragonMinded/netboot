all: hellonaomi hellocpp inputtest eepromtest audiotest netdimm rtctest threadtest debugprint carttest

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

.PHONY: audiotest
audiotest:
	$(MAKE) -C audiotest

.PHONY: netdimm
netdimm:
	$(MAKE) -C netdimm

.PHONY: rtctest
rtctest:
	$(MAKE) -C rtctest

.PHONY: threadtest
threadtest:
	$(MAKE) -C threadtest

.PHONY: debugprint
debugprint:
	$(MAKE) -C debugprint

.PHONY: carttest
carttest:
	$(MAKE) -C carttest

.PHONY: copy
copy: hellonaomi hellocpp inputtest eepromtest audiotest netdimm rtctest threadtest debugprint carttest
	cp -r hellonaomi/hellonaomi.bin ../binaries/
	cp -r hellocpp/hellocpp.bin ../binaries/
	cp -r inputtest/inputtest.bin ../binaries/
	cp -r eepromtest/eepromtest.bin ../binaries/
	cp -r audiotest/audiotest.bin ../binaries/
	cp -r netdimm/netdimm.bin ../binaries/
	cp -r rtctest/rtctest.bin ../binaries/
	cp -r threadtest/threadtest.bin ../binaries/
	cp -r debugprint/debugprint.bin ../binaries/
	cp -r carttest/carttest.bin ../binaries/

.PHONY: clean
clean:
	$(MAKE) -C hellonaomi clean
	$(MAKE) -C hellocpp clean
	$(MAKE) -C inputtest clean
	$(MAKE) -C eepromtest clean
	$(MAKE) -C audiotest clean
	$(MAKE) -C netdimm clean
	$(MAKE) -C rtctest clean
	$(MAKE) -C threadtest clean
	$(MAKE) -C debugprint clean
	$(MAKE) -C carttest clean
