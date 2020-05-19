all: testrom.bin

CC = sh-elf-gcc -ml -nostdlib -nostartfiles -fno-stack-protector -ffreestanding
LD = sh-elf-ld -mshlelf -Tnaomi.ld
AS = sh-elf-as --isa sh4 -little
OBJCOPY = sh-elf-objcopy

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
	${LD} -o $@ $^

build/crt0.o: crt0.S | build
	${CC} -E crt0.S -o build/crt0.s
	${AS} -o build/crt0.o build/crt0.s

build/main.o: main.c | build
	${CC} -c main.c -o build/main.o

clean:
	rm -rf build
	rm -rf testrom.bin
