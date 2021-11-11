all: pyenv libnaomi libnaomimessage examples tests

${NAOMI_BASE}/tools/pyenv:
	mkdir -p ${NAOMI_BASE}/tools
	cd ${NAOMI_BASE}/tools && python3 -m venv pyenv
	${NAOMI_BASE}/tools/pyenv/bin/python3 -m pip install wheel pillow
	cd ../ && ${NAOMI_BASE}/tools/pyenv/bin/python3 -m pip install --no-warn-conflicts .

.PHONY: pyenv
pyenv: ${NAOMI_BASE}/tools/pyenv

.PHONY: libnaomi
libnaomi:
	$(MAKE) -C libnaomi

.PHONY: libnaomimessage
libnaomimessage:
	$(MAKE) -C libnaomi/message

.PHONY: examples
examples: libnaomi libnaomimessage
	$(MAKE) -C examples

.PHONY: tests
tests: libnaomi libnaomimessage
	$(MAKE) -C tests

.PHONY: copy
copy: libnaomi libnaomimessage examples
	$(MAKE) -C examples copy

.PHONY: wipe-install-venv
wipe-install-venv:
	mkdir -p ${NAOMI_BASE}/tools
	cd ${NAOMI_BASE}/tools && rm -rf pyenv
	cd ${NAOMI_BASE}/tools && python3 -m venv pyenv
	${NAOMI_BASE}/tools/pyenv/bin/python3 -m pip install wheel pillow
	cd ../ && ${NAOMI_BASE}/tools/pyenv/bin/python3 -m pip install --no-warn-conflicts .

.PHONY: install
install: libnaomi libnaomimessage wipe-install-venv
	$(MAKE) -C libnaomi install
	$(MAKE) -C libnaomi/message install
	mkdir -p ${NAOMI_BASE}/tools
	cp naomi.ld ${NAOMI_BASE}/tools
	cp aica.ld ${NAOMI_BASE}/tools
	cp Makefile.external.base ${NAOMI_BASE}/tools/Makefile.base
	cp tools/*.py ${NAOMI_BASE}/tools
	cp ../scripts/makerom.py ${NAOMI_BASE}/tools

.PHONY: clean
clean:
	$(MAKE) -C libnaomi clean
	$(MAKE) -C libnaomi/message clean
	$(MAKE) -C examples clean
	$(MAKE) -C tests clean
