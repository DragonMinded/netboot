# Please see the README for setting up a valid build environment.
all: hellonaomi.bin

CC = ${NAOMI_SH_CC} -ml -nostdlib -nostartfiles -fno-stack-protector -ffreestanding -fomit-frame-pointer -ffunction-sections -fdata-sections
LD = ${NAOMI_SH_LD} -mshlelf -Tnaomi.ld --gc-sections -L${NAOMI_SH_BASE}/sh-elf/lib/ -L${NAOMI_SH_BASE}/lib/gcc/sh-elf/${NAOMI_SH_GCC_VER}/
AS = ${NAOMI_SH_AS} --isa sh4 -little
OBJCOPY = ${NAOMI_SH_OBJCOPY}

hellonaomi.bin: build/naomi.bin
	PYTHONPATH=../../../ python3 -m scripts.makerom $@ \
		--title "Hello Naomi" \
		--publisher "DragonMinded" \
		--section $<,0xc021000 \
		--entrypoint 0xc021000 \
		--test-entrypoint 0xc021000

build:
	mkdir -p build

build/naomi.bin: build/naomi.elf
	${OBJCOPY} $< $@ -O binary

build/naomi.elf: build/crt0.o build/main.o build/system.o
	${LD} -o $@ $^ -lc -lgcc

build/crt0.o: crt0.s | build
	${AS} -o build/crt0.o crt0.s

build/main.o: main.c | build
	${CC} -c main.c -o build/main.o

build/system.o: system.c | build
	${CC} -c system.c -o build/system.o

clean:
	rm -rf build
	rm -rf hellonaomi.bin
