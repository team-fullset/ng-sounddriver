CC = sdcc
AS = sdasz80
LD = sdldz80
OBJC = sdobjcopy

CCFLAGS = -mz80 --no-std-crt0

.PHONY: install clean

build/driver.bin: build/driver.ihx
	$(OBJC) -I ihex -O binary --pad-to=0x4000 build/driver.ihx $@

build/driver.ihx: build/start.rel build/main.rel
	$(LD) -i -b _HEADER=0x0000 -b _CODE=0x0200 $@ build/main.rel build/start.rel

build/main.rel: main.c
	$(CC) $(CCFLAGS) -c -o $@ $<

build/start.rel: start.s
	$(AS) -l -o $@ $<

install:
	cat build/driver.bin ../../devkit/sndtools/datasection.bin > ../../sound/sound.M1

clean:
	rm -rf build/*
