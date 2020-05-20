# Please see the README for setting up a valid build environment.
all: testrom.bin

NAOMI_BASE = /opt/toolchains/naomi
NAOMI_SH_BASE = ${NAOMI_BASE}/sh-elf
NAOMI_ARM_BASE = ${NAOMI_BASE}/arm-eab
NAOMI_SH_GCC_VER = 9.3.0
NAOMI_ARM_GCC_VER = 8.4.0

CC = ${NAOMI_SH_BASE}/bin/sh-elf-gcc -ml -nostdlib -nostartfiles -fno-stack-protector -ffreestanding -ffunction-sections -fdata-sections
LD = ${NAOMI_SH_BASE}/bin/sh-elf-ld -mshlelf -Tnaomi.ld -L${NAOMI_SH_BASE}/sh-elf/lib/ -L${NAOMI_SH_BASE}/lib/gcc/sh-elf/${NAOMI_SH_GCC_VER}/
AS = ${NAOMI_SH_BASE}/bin/sh-elf-as --isa sh4 -little
OBJCOPY = ${NAOMI_SH_BASE}/bin/sh-elf-objcopy

testrom.bin: build/test.bin
	PYTHONPATH=../ python3 -m scripts.makerom testrom.bin \
		--title "Test ROM" \
		--publisher "DragonMinded" \
		--section build/test.bin,0x8c020000 \
		--entrypoint 0x8c020000 \
		--test-entrypoint 0x8c020000

build:
	mkdir -p build

build/test.bin: build/test.elf
	${OBJCOPY} $< $@ -O binary

build/test.elf: build/crt0.o build/main.o
	${LD} -o $@ $^ -lc -lgcc

build/crt0.o: crt0.S | build
	${CC} -E crt0.S -o build/crt0.s
	${AS} -o build/crt0.o build/crt0.s

build/main.o: main.c | build
	${CC} -c main.c -o build/main.o

clean:
	rm -rf build
	rm -rf testrom.bin
