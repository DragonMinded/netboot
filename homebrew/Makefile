all: testrom.bin

testrom.bin: build/test.bin
	PYTHONPATH=../ python3 -m scripts.makerom testrom.bin \
		--title "Test ROM" \
		--publisher "DragonMinded" \
		--section build/test.bin,0x0C021000 \
		--entrypoint 0x0C021000 \
		--test-entrypoint 0x0C021000

build:
	mkdir -p build

build/test.bin: build/crt0.o build/main.o
	sh4-linux-gnu-ld -e start --oformat binary -Ttext 0x0C021000 -o build/test.bin build/crt0.o build/main.o

build/crt0.o: crt0.S | build
	sh4-linux-gnu-gcc -E crt0.S -o build/crt0.s
	sh4-linux-gnu-as --isa sh4 -o build/crt0.o build/crt0.s

build/main.o: main.c | build
	sh4-linux-gnu-gcc -nostdlib -nostartfiles -c main.c -o build/main.o

clean:
	rm -rf build
	rm -rf testrom.bin
